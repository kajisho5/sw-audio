// SW AUDIO — generic CLAP adapter: any product core + the common frame (sw::Shell) -> CLAP plugin.
// A product supplies a traits struct P:
//   using Core = ...;                                   // prepare / setParam / snapToTargets / process / latencySamples
//   static const std::vector<sw::ParamSpec>& specs();   // product parameter table (spec order, never reordered)
//   static constexpr int kOutputParam, kInParam, kMixParam; // routed to the shell (-1 if absent)
// Optional on Core: void setTempo(double bpm) — receives the host tempo when the transport provides it.
//   static const clap_plugin_descriptor_t* descriptor();
// Host-facing values (spec 共通章 2): continuous = normalized 0..1, stepped = step index.
// Host parameter ids: product params 0..N-1, then common params (Auto gain, Delta) appended, so ids stay stable.
#pragma once
#include "sw/denormal.hpp"
#include "sw/param.hpp"
#include "sw/shell.hpp"
#include "sw/text.hpp"
#include <clap/clap.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

namespace sw::clapad {

// optional trait: static constexpr bool kAutoGain = false; -> the product has no Auto gain parameter
template <class P, class = void> struct AutoGainEnabled : std::true_type {};
template <class P> struct AutoGainEnabled<P, std::void_t<decltype(P::kAutoGain)>> : std::bool_constant<P::kAutoGain> {};

template <class C, class = void> struct HasSetTempo : std::false_type {};
template <class C> struct HasSetTempo<C, std::void_t<decltype(std::declval<C&>().setTempo(120.0))>> : std::true_type {};

inline const ParamSpec& autoGainSpec() { static const ParamSpec s{"common.autogain", "Auto gain", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}}; return s; }
inline const ParamSpec& deltaSpec() { static const ParamSpec s{"common.delta", "Delta", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}}; return s; }

template <class P>
class Plugin {
public:
    static constexpr bool kHasAutoGain = AutoGainEnabled<P>::value;
    static int numProduct() { return static_cast<int>(P::specs().size()); }
    static int autoGainId() { return kHasAutoGain ? numProduct() : -1; }
    static int deltaId() { return numProduct() + (kHasAutoGain ? 1 : 0); }
    static int numParams() { return deltaId() + 1; }
    static const ParamSpec& spec(int id) {
        if (id < numProduct()) return P::specs()[static_cast<size_t>(id)];
        return id == autoGainId() ? autoGainSpec() : deltaSpec();
    }
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

    void apply(int id, double plain) {
        if (id == P::kOutputParam) shell_.setOutputDb(plain);
        else if (id == P::kInParam) shell_.setIn(plain > 0.5);
        else if (id == P::kMixParam) shell_.setMix(plain / 100.0);  // Mix is in percent
        else if (id == autoGainId()) shell_.setAutoGain(plain > 0.5);
        else if (id == deltaId()) shell_.setDelta(plain > 0.5);
        if (id < numProduct()) shell_.core().setParam(id, plain);
    }
    void applyPending() {
        for (int i = 0; i < numParams(); ++i)
            if (dirty_[static_cast<size_t>(i)].exchange(false)) apply(i, hostToPlain(i, host_values_[static_cast<size_t>(i)].load()));
        // snapping needs prepared DSP (e.g. EQ08 builds its kernel): before activation it waits for activate()
        if (active_ && snap_pending_.exchange(false)) { shell_.core().snapToTargets(); shell_.snap(); }
    }
    void handleEvent(const clap_event_header_t* h) {
        if (h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) return;
        const auto* ev = reinterpret_cast<const clap_event_param_value_t*>(h);
        if (ev->param_id >= static_cast<clap_id>(numParams())) return;
        const int id = static_cast<int>(ev->param_id);
        host_values_[static_cast<size_t>(id)].store(ev->value);
        apply(id, hostToPlain(id, ev->value));
    }

    // ---- plugin
    static bool init(const clap_plugin_t*) { return true; }
    static void destroy(const clap_plugin_t* p) { delete self(p); }
    static bool activate(const clap_plugin_t* p, double sr, uint32_t, uint32_t maxFrames) {
        Plugin* s = self(p);
        s->shell_.prepare(sr, static_cast<int>(maxFrames), 2);
        for (int i = 0; i < numParams(); ++i) s->dirty_[static_cast<size_t>(i)].store(true);
        s->snap_pending_.store(true);
        s->active_ = true;
        s->applyPending();
        s->restart_requested_.store(false);
        return true;
    }
    static void deactivate(const clap_plugin_t* p) { self(p)->active_ = false; }
    static bool startProcessing(const clap_plugin_t*) { return true; }
    static void stopProcessing(const clap_plugin_t*) {}
    static void reset(const clap_plugin_t* p) { self(p)->snap_pending_.store(true); }

    static clap_process_status process(const clap_plugin_t* p, const clap_process_t* pr) {
        ScopedNoDenormals noDenormals;
        Plugin* s = self(p);
        s->applyPending();
        if (pr->audio_inputs_count < 1 || pr->audio_outputs_count < 1) return CLAP_PROCESS_ERROR;
        const clap_audio_buffer_t& ib = pr->audio_inputs[0];
        clap_audio_buffer_t& ob = pr->audio_outputs[0];
        const uint32_t nch = std::min(ib.channel_count, ob.channel_count);
        if (!ib.data32 || !ob.data32 || nch == 0) return CLAP_PROCESS_ERROR;
        const uint32_t frames = pr->frames_count;
        if constexpr (HasSetTempo<typename P::Core>::value)
            if (pr->transport && (pr->transport->flags & CLAP_TRANSPORT_HAS_TEMPO)) s->shell_.core().setTempo(pr->transport->tempo);
        for (uint32_t c = 0; c < nch; ++c)
            if (ob.data32[c] != ib.data32[c]) std::memcpy(ob.data32[c], ib.data32[c], frames * sizeof(float));
        // optional sidechain (host may leave it unconnected)
        const float* scBase[2] = {nullptr, nullptr};
        int scCh = 0;
        if (kSidechain && pr->audio_inputs_count >= 2 && pr->audio_inputs[1].data32 && pr->audio_inputs[1].channel_count > 0) {
            scCh = static_cast<int>(std::min<uint32_t>(2, pr->audio_inputs[1].channel_count));
            for (int c = 0; c < scCh; ++c) scBase[c] = pr->audio_inputs[1].data32[c];
        }
        const uint32_t nev = pr->in_events->size(pr->in_events);
        uint32_t ev = 0, pos = 0;
        while (pos < frames) {  // sample-accurate parameter events
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
            const float* sc[2] = {scCh > 0 ? scBase[0] + pos : nullptr, scCh > 1 ? scBase[1] + pos : nullptr};
            s->shell_.process(chans, static_cast<int>(nch), static_cast<int>(next - pos), scCh > 0 ? sc : nullptr, scCh);
            pos = next;
        }
        // a parameter changed the latency (e.g. Lookahead): CLAP only allows that across a restart
        if (s->shell_.core().latencySamples() != s->shell_.latencySamples() && !s->restart_requested_.exchange(true))
            if (s->host_ && s->host_->request_restart) s->host_->request_restart(s->host_);
        return CLAP_PROCESS_CONTINUE;
    }

    static const void* getExtension(const clap_plugin_t*, const char* id) {
        static const clap_plugin_audio_ports_t ports = {portsCount, portsGet};
        static const clap_plugin_params_t params = {paramsCount, paramsInfo, paramsValue, paramsToText, paramsFromText, paramsFlush};
        static const clap_plugin_state_t state = {stateSave, stateLoad};
        static const clap_plugin_latency_t latency = {latencyGet};
        if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &ports;
        if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &params;
        if (!std::strcmp(id, CLAP_EXT_STATE)) return &state;
        if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &latency;
        return nullptr;
    }
    static void onMainThread(const clap_plugin_t*) {}

    // ---- audio ports: one stereo in, one stereo out (+ a stereo sidechain input when the core accepts one)
    static constexpr bool kSidechain = AcceptsSidechain<typename P::Core>::value;
    static uint32_t portsCount(const clap_plugin_t*, bool isInput) { return isInput && kSidechain ? 2 : 1; }
    static bool portsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
        if (isInput && kSidechain && index == 1) {
            info->id = 2;
            std::snprintf(info->name, sizeof(info->name), "%s", "Sidechain");
            info->flags = 0;
            info->channel_count = 2;
            info->port_type = CLAP_PORT_STEREO;
            info->in_place_pair = CLAP_INVALID_ID;
            return true;
        }
        if (index != 0) return false;
        info->id = isInput ? 0 : 1;
        std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Input" : "Output");
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = isInput ? 1 : 0;
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
        for (uint32_t i = 0, n = in->size(in); i < n; ++i) self(p)->handleEvent(in->get(in, i));
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
        pl->snap_pending_.store(true);
        if (!pl->active_) pl->applyPending();
        if (pl->host_) {  // CLAP: values changed -> tell the host (main thread)
            const auto* hp = static_cast<const clap_host_params_t*>(pl->host_->get_extension(pl->host_, CLAP_EXT_PARAMS));
            if (hp && hp->rescan) hp->rescan(pl->host_, CLAP_PARAM_RESCAN_VALUES);
        }
        return true;
    }

    // ---- latency
    static uint32_t latencyGet(const clap_plugin_t* p) { return static_cast<uint32_t>(self(p)->shell_.latencySamples()); }

    clap_plugin_t plugin_{};
    const clap_host_t* host_ = nullptr;
    Shell<typename P::Core> shell_;
    std::vector<std::atomic<double>> host_values_;
    std::vector<std::atomic<bool>> dirty_;
    std::atomic<bool> snap_pending_{true};
    std::atomic<bool> restart_requested_{false};
    bool active_ = false;
};

// single-plugin factory helper
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

}  // namespace sw::clapad

// defines the three entry functions referenced by the generated <code>_entry.cpp
#define SW_CLAP_ENTRY(code, Traits)                                                           \
    bool sw_##code##_entry_init(const char*) { return true; }                                 \
    void sw_##code##_entry_deinit() {}                                                        \
    const void* sw_##code##_entry_get_factory(const char* id) {                               \
        return !std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? sw::clapad::Factory<Traits>::get() : nullptr; \
    }
