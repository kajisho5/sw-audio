// SW AUDIO — CLAP adapter for instruments (SW IN07 SWINGBY): note input -> synth core -> stereo output.
// A product supplies a traits struct P:
//   using Core = ...;   // prepare / setParam / noteOn(key, vel, channel, noteId) / noteOff(key, channel) / choke / pitchBend(-1..1)
//                       // sustain(bool) / allNotesOff / allSoundOff / process(float**, nch, n) / takeEnded(key, channel, noteId)
//                       // optional: setTempo(bpm) — called with the host tempo when the transport has one
//   static const std::vector<sw::ParamSpec>& specs();   // parameter table (never reordered; new ones are appended)
//   static const clap_plugin_descriptor_t* descriptor();
// Host-facing values as the effects (spec 共通章 2): continuous = normalized 0..1, stepped = step index. State: "SWA1" + count + values.
// Notes: one note port, CLAP and MIDI dialects (CLAP preferred). MIDI: note on / off, pitch bend, CC 1 mod wheel, CC 64 sustain,
// CC 120 all sound off, CC 123 all notes off, channel pressure; CLAP pressure expressions act as the aftertouch. Events are sample accurate: the block is split at each event. Every note that ends is reported (CLAP note end).
// No plug-in window yet (the screen comes later): hosts show their generic controls. The instruments have no Auto gain, Delta, In or Mix.
#pragma once
#include "sw/denormal.hpp"
#include "sw/param.hpp"
#include "sw/text.hpp"
#include <clap/clap.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

namespace sw::clapinst {

// optional on Core: void setTempo(double bpm) — the host tempo (tempo-synced effects)
template <class C, class = void> struct HasSetTempo : std::false_type {};
template <class C> struct HasSetTempo<C, std::void_t<decltype(std::declval<C&>().setTempo(120.0))>> : std::true_type {};
// optional on Core: modWheel(0..1) (CC 1) and aftertouch(0..1) (channel pressure, CLAP pressure expression)
template <class C, class = void> struct HasModWheel : std::false_type {};
template <class C> struct HasModWheel<C, std::void_t<decltype(std::declval<C&>().modWheel(0.0))>> : std::true_type {};
template <class C, class = void> struct HasAftertouch : std::false_type {};
template <class C> struct HasAftertouch<C, std::void_t<decltype(std::declval<C&>().aftertouch(0.0))>> : std::true_type {};

template <class P>
class Plugin {
public:
    static int numParams() { return static_cast<int>(P::specs().size()); }
    static const ParamSpec& spec(int id) { return P::specs()[static_cast<size_t>(id)]; }
    static bool stepped(int id) { return spec(id).curve == Curve::Step; }
    static double hostToPlain(int id, double h) {
        const auto& s = spec(id);
        if (stepped(id)) return s.steps[static_cast<size_t>(std::clamp(static_cast<int>(std::lround(h)), 0, s.numSteps() - 1))];
        return s.toValue(h);
    }
    static double plainToHost(int id, double v) {
        const auto& s = spec(id);
        return stepped(id) ? static_cast<double>(std::lround(s.toNorm(v) * (s.numSteps() - 1))) : s.toNorm(v);
    }

    explicit Plugin(const clap_host_t* host) : host_(host), host_values_(static_cast<size_t>(numParams())), dirty_(static_cast<size_t>(numParams())) {
        for (int i = 0; i < numParams(); ++i) { host_values_[static_cast<size_t>(i)].store(plainToHost(i, spec(i).def)); dirty_[static_cast<size_t>(i)].store(true); }
        plugin_ = {P::descriptor(), this, init, destroy, activate, deactivate, startProcessing, stopProcessing, reset, process, getExtension, onMainThread};
    }
    const clap_plugin_t* clap() const { return &plugin_; }

private:
    static Plugin* self(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

    void applyPending() {
        for (int i = 0; i < numParams(); ++i)
            if (dirty_[static_cast<size_t>(i)].exchange(false)) core_.setParam(i, hostToPlain(i, host_values_[static_cast<size_t>(i)].load()));
    }
    void handleEvent(const clap_event_header_t* h) {
        if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
        switch (h->type) {
            case CLAP_EVENT_PARAM_VALUE: {
                const auto* ev = reinterpret_cast<const clap_event_param_value_t*>(h);
                if (ev->param_id >= static_cast<clap_id>(numParams())) return;
                const int id = static_cast<int>(ev->param_id);
                host_values_[static_cast<size_t>(id)].store(ev->value);
                core_.setParam(id, hostToPlain(id, ev->value));
                return;
            }
            case CLAP_EVENT_NOTE_ON: {
                const auto* ev = reinterpret_cast<const clap_event_note_t*>(h);
                if (ev->key >= 0 && ev->key <= 127) core_.noteOn(ev->key, ev->velocity, ev->channel < 0 ? 0 : ev->channel, ev->note_id);
                return;
            }
            case CLAP_EVENT_NOTE_OFF: {
                const auto* ev = reinterpret_cast<const clap_event_note_t*>(h);
                core_.noteOff(ev->key, ev->channel);
                return;
            }
            case CLAP_EVENT_NOTE_CHOKE: {
                const auto* ev = reinterpret_cast<const clap_event_note_t*>(h);
                core_.choke(ev->key, ev->channel);
                return;
            }
            case CLAP_EVENT_NOTE_EXPRESSION: {   // pressure: taken as the (global) aftertouch
                const auto* ev = reinterpret_cast<const clap_event_note_expression_t*>(h);
                if constexpr (HasAftertouch<typename P::Core>::value)
                    if (ev->expression_id == CLAP_NOTE_EXPRESSION_PRESSURE) core_.aftertouch(std::clamp(ev->value, 0.0, 1.0));
                return;
            }
            case CLAP_EVENT_MIDI: {
                const auto* ev = reinterpret_cast<const clap_event_midi_t*>(h);
                const int st = ev->data[0] & 0xF0, ch = ev->data[0] & 0x0F, d1 = ev->data[1] & 0x7F, d2 = ev->data[2] & 0x7F;
                if (st == 0x90 && d2 > 0) core_.noteOn(d1, d2 / 127.0, ch, -1);
                else if (st == 0x80 || (st == 0x90 && d2 == 0)) core_.noteOff(d1, ch);
                else if (st == 0xE0) core_.pitchBend(std::clamp(((d2 << 7) | d1) - 8192, -8191, 8191) / 8191.0);
                else if (st == 0xB0 && d1 == 64) core_.sustain(d2 >= 64);
                else if (st == 0xB0 && d1 == 1) { if constexpr (HasModWheel<typename P::Core>::value) core_.modWheel(d2 / 127.0); }
                else if (st == 0xD0) { if constexpr (HasAftertouch<typename P::Core>::value) core_.aftertouch(d1 / 127.0); }
                else if (st == 0xB0 && d1 == 120) core_.allSoundOff();
                else if (st == 0xB0 && d1 == 123) core_.allNotesOff();
                return;
            }
            default: return;
        }
    }
    void emitNoteEnds(const clap_output_events_t* out, uint32_t time) {
        int key = 0, ch = 0, id = -1;
        while (core_.takeEnded(key, ch, id)) {
            if (!out) continue;
            clap_event_note_t e{}; e.header.size = sizeof(e); e.header.time = time; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            e.header.type = CLAP_EVENT_NOTE_END; e.header.flags = 0;
            e.note_id = id; e.port_index = 0; e.channel = static_cast<int16_t>(ch); e.key = static_cast<int16_t>(key); e.velocity = 0.0;
            out->try_push(out, &e.header);
        }
    }

    // ---- plugin
    static bool init(const clap_plugin_t*) { return true; }
    static void destroy(const clap_plugin_t* p) { delete self(p); }
    static bool activate(const clap_plugin_t* p, double sr, uint32_t, uint32_t maxFrames) {
        Plugin* s = self(p);
        s->core_.prepare(sr, static_cast<int>(std::max<uint32_t>(1, maxFrames)));
        for (int i = 0; i < numParams(); ++i) s->dirty_[static_cast<size_t>(i)].store(true);
        s->applyPending();
        s->active_ = true;
        return true;
    }
    static void deactivate(const clap_plugin_t* p) { self(p)->active_ = false; }
    static bool startProcessing(const clap_plugin_t*) { return true; }
    static void stopProcessing(const clap_plugin_t*) {}
    static void reset(const clap_plugin_t* p) { self(p)->core_.allSoundOff(); }

    static clap_process_status process(const clap_plugin_t* p, const clap_process_t* pr) {
        ScopedNoDenormals noDenormals;
        Plugin* s = self(p);
        s->applyPending();
        if (pr->audio_outputs_count < 1) return CLAP_PROCESS_ERROR;
        clap_audio_buffer_t& ob = pr->audio_outputs[0];
        const uint32_t nch = ob.channel_count;
        if (!ob.data32 || nch == 0) return CLAP_PROCESS_ERROR;
        const uint32_t frames = pr->frames_count;
        if constexpr (HasSetTempo<typename P::Core>::value)
            if (pr->transport && (pr->transport->flags & CLAP_TRANSPORT_HAS_TEMPO)) s->core_.setTempo(pr->transport->tempo);
        const uint32_t nev = pr->in_events ? pr->in_events->size(pr->in_events) : 0;
        uint32_t ev = 0, pos = 0;
        while (pos < frames) {   // sample-accurate events: render up to each one
            while (ev < nev) {
                const clap_event_header_t* h = pr->in_events->get(pr->in_events, ev);
                if (h->time > pos) break;
                s->handleEvent(h);
                ++ev;
            }
            uint32_t next = frames;
            if (ev < nev) next = std::min(frames, pr->in_events->get(pr->in_events, ev)->time);
            if (next <= pos) next = pos + 1;
            float* chans[2] = {ob.data32[0] + pos, nch > 1 ? ob.data32[1] + pos : ob.data32[0] + pos};
            s->core_.process(chans, static_cast<int>(std::min<uint32_t>(nch, 2)), static_cast<int>(next - pos));
            pos = next;
        }
        while (ev < nev) { s->handleEvent(pr->in_events->get(pr->in_events, ev)); ++ev; }   // events at or after the end (frames 0)
        for (uint32_t c = 2; c < nch; ++c) if (ob.data32[c]) std::memset(ob.data32[c], 0, frames * sizeof(float));
        s->emitNoteEnds(pr->out_events, frames > 0 ? frames - 1 : 0);
        return CLAP_PROCESS_CONTINUE;
    }

    static const void* getExtension(const clap_plugin_t*, const char* id) {
        static const clap_plugin_audio_ports_t ports = {portsCount, portsGet};
        static const clap_plugin_note_ports_t notePorts = {notePortsCount, notePortsGet};
        static const clap_plugin_params_t params = {paramsCount, paramsInfo, paramsValue, paramsToText, paramsFromText, paramsFlush};
        static const clap_plugin_state_t state = {stateSave, stateLoad};
        if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &ports;
        if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &notePorts;
        if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &params;
        if (!std::strcmp(id, CLAP_EXT_STATE)) return &state;
        return nullptr;
    }
    static void onMainThread(const clap_plugin_t*) {}

    // ---- ports: notes in, one stereo out
    static uint32_t portsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0 : 1; }
    static bool portsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
        if (isInput || index != 0) return false;
        info->id = 1;
        std::snprintf(info->name, sizeof(info->name), "%s", "Output");
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        return true;
    }
    static uint32_t notePortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 1 : 0; }
    static bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info) {
        if (!isInput || index != 0) return false;
        info->id = 0;
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        std::snprintf(info->name, sizeof(info->name), "%s", "Notes");
        return true;
    }

    // ---- params
    static uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(numParams()); }
    static bool paramsInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
        if (index >= static_cast<uint32_t>(numParams())) return false;
        const int id = static_cast<int>(index);
        const auto& s = spec(id);
        std::memset(info, 0, sizeof(*info));
        info->id = index;
        info->flags = (s.automatable ? CLAP_PARAM_IS_AUTOMATABLE : 0) | (stepped(id) ? CLAP_PARAM_IS_STEPPED : 0);
        std::snprintf(info->name, sizeof(info->name), "%s", s.name);
        std::snprintf(info->module, sizeof(info->module), "%s", s.id);
        info->min_value = 0;
        info->max_value = stepped(id) ? s.numSteps() - 1 : 1;
        info->default_value = plainToHost(id, s.def);
        return true;
    }
    static bool paramsValue(const clap_plugin_t* p, clap_id id, double* out) {
        if (id >= static_cast<clap_id>(numParams())) return false;
        *out = self(p)->host_values_[id].load();
        return true;
    }
    static bool paramsToText(const clap_plugin_t*, clap_id id, double value, char* out, uint32_t cap) {
        if (id >= static_cast<clap_id>(numParams()) || cap == 0) return false;
        const int i = static_cast<int>(id);
        std::snprintf(out, cap, "%s", formatValue(spec(i), hostToPlain(i, value)).c_str());
        return true;
    }
    static bool paramsFromText(const clap_plugin_t*, clap_id id, const char* text, double* out) {
        if (id >= static_cast<clap_id>(numParams()) || !text) return false;
        double v = 0;
        if (!parseValue(spec(static_cast<int>(id)), text, v)) return false;
        *out = plainToHost(static_cast<int>(id), v);
        return true;
    }
    static void paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*) {
        Plugin* s = self(p);
        for (uint32_t i = 0, n = in ? in->size(in) : 0; i < n; ++i) {
            const clap_event_header_t* h = in->get(in, i);
            if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE) s->handleEvent(h);   // only values here: no notes outside process
        }
        s->applyPending();
    }

    // ---- state: "SWA1" + count + host values (count-based, so appended params load old states)
    static bool writeAll(const clap_ostream_t* s, const void* d, uint64_t n) {
        const char* c = static_cast<const char*>(d);
        while (n > 0) { const int64_t w = s->write(s, c, n); if (w <= 0) return false; c += w; n -= static_cast<uint64_t>(w); }
        return true;
    }
    static bool readAll(const clap_istream_t* s, void* d, uint64_t n) {
        char* c = static_cast<char*>(d);
        while (n > 0) { const int64_t r = s->read(s, c, n); if (r <= 0) return false; c += r; n -= static_cast<uint64_t>(r); }
        return true;
    }
    static bool stateSave(const clap_plugin_t* p, const clap_ostream_t* s) {
        const char magic[4] = {'S', 'W', 'A', '1'};
        const uint32_t count = static_cast<uint32_t>(numParams());
        if (!writeAll(s, magic, 4) || !writeAll(s, &count, 4)) return false;
        for (int i = 0; i < numParams(); ++i) { const double v = self(p)->host_values_[static_cast<size_t>(i)].load(); if (!writeAll(s, &v, 8)) return false; }
        return true;
    }
    static bool stateLoad(const clap_plugin_t* p, const clap_istream_t* s) {
        char magic[4]; uint32_t count = 0;
        if (!readAll(s, magic, 4) || std::memcmp(magic, "SWA1", 4) != 0 || !readAll(s, &count, 4)) return false;
        Plugin* pl = self(p);
        for (uint32_t i = 0; i < count; ++i) {
            double v; if (!readAll(s, &v, 8)) return false;
            if (i < static_cast<uint32_t>(numParams())) { pl->host_values_[i].store(v); pl->dirty_[i].store(true); }
        }
        if (!pl->active_) pl->applyPending();
        if (pl->host_) {  // CLAP: values changed -> tell the host (main thread)
            const auto* hp = static_cast<const clap_host_params_t*>(pl->host_->get_extension(pl->host_, CLAP_EXT_PARAMS));
            if (hp && hp->rescan) hp->rescan(pl->host_, CLAP_PARAM_RESCAN_VALUES);
        }
        return true;
    }

    clap_plugin_t plugin_{};
    const clap_host_t* host_ = nullptr;
    typename P::Core core_;
    std::vector<std::atomic<double>> host_values_;
    std::vector<std::atomic<bool>> dirty_;
    bool active_ = false;
};

template <class P>
struct Factory {
    static uint32_t count(const clap_plugin_factory_t*) { return 1; }
    static const clap_plugin_descriptor_t* desc(const clap_plugin_factory_t*, uint32_t i) { return i == 0 ? P::descriptor() : nullptr; }
    static const clap_plugin_t* create(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
        if (!clap_version_is_compatible(host->clap_version) || std::strcmp(id, P::descriptor()->id) != 0) return nullptr;
        return (new Plugin<P>(host))->clap();
    }
    static const clap_plugin_factory_t* get() {
        static const clap_plugin_factory_t f = {count, desc, create};
        return &f;
    }
};

}  // namespace sw::clapinst

#define SW_CLAP_INSTRUMENT_ENTRY(code, Traits)                                                    \
    bool sw_##code##_entry_init(const char*) { return true; }                                     \
    void sw_##code##_entry_deinit() {}                                                            \
    const void* sw_##code##_entry_get_factory(const char* id) {                                   \
        return !std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? sw::clapinst::Factory<Traits>::get() : nullptr; \
    }
