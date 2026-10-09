// SW AUDIO — CLAP adapter for instruments (SW IN07 SWINGBY): note input -> synth core -> stereo output.
// A product supplies a traits struct P:
//   using Core = ...;   // prepare / setParam / noteOn(key, vel, channel, noteId) / noteOff(key, channel) / choke / pitchBend(-1..1)
//                       // sustain(bool) / allNotesOff / allSoundOff / process(float**, nch, n) / takeEnded(key, channel, noteId)
//                       // optional: setTempo(bpm) — called with the host tempo when the transport has one
//   static const std::vector<sw::ParamSpec>& specs();   // parameter table (never reordered; new ones are appended)
//   static const clap_plugin_descriptor_t* descriptor();
//   optional, a program (preset) selector: static constexpr int kProgramParam; static void loadProgram(Core&, int step) (audio thread or flush:
//   no allocation); static void warmUp() (activate: build tables). Needs Core::param(id) (plain values) to read the loaded values back.
//   When the host changes the selector, the preset is loaded and every value it changed is reported to the host (CLAP param value events);
//   a restored state sets the selector without loading (the session keeps its own values).
//   optional on Core: beginPatch() / endPatch(): a program load, a preset from the host's browser and a restored state go in as one patch
//   (the core fades and starts the held notes again with it: no step).
//   optional, preset files (CLAP preset-load and the preset-discovery factory: a host's browser lists the factory presets and the user's
//   files, and loads them): kPresetVendor / kPresetProduct (the user folder, plugin/clap/user_presets.hpp), factoryPresetCount(),
//   factoryPresetName(i), factoryPresetCategory(i), factoryPresetValues(i, plain), userPresetValues(text, plain, meta, error).
//   A preset is loaded on the main thread into the host values (as a restored state is) and reported with a parameter rescan; a factory
//   preset also moves the selector to it. A file is read as untrusted input (size limit, strict parsing: sw/preset_file.hpp).
// Host-facing values as the effects (spec 共通章 2): continuous = normalized 0..1, stepped = step index. State: "SWA1" + count + values.
// Values from the host or a saved state are sanitized (not finite -> the default, then clamped into range); a state is read whole before
// anything changes, so a cut or damaged state changes nothing.
// Notes: one note port, CLAP and MIDI dialects (CLAP preferred). MIDI: note on / off, pitch bend, CC 1 mod wheel, CC 64 sustain,
// CC 120 all sound off, CC 123 all notes off, channel pressure; CLAP pressure expressions act as the aftertouch. Events are sample accurate: the block is split at each event. Every note that ends is reported (CLAP note end).
// The plug-in window (optional: static const sw::instgui::Ui& ui() in P, with the preset files above): the page of plugin/clap/inst_gui.hpp
// in the platform's web view (gui_mac.mm, gui_win.cpp; none on Linux, where hosts show their generic controls). The page's edits go to
// the host as gestures and values (automation), its notes to the synth, its preset calls (factory, user, save), the licence (the
// machine code, a licence file or what the activation server sends back) and the window settings are handled on the main thread.
// The state carries, after the values, tagged chunks older versions read past: "SWNM" the preset in use (JSON), "SWCC" the MIDI learn
// (128 bytes: the macro each controller moves, -1 = none). MIDI Program Change n loads factory preset n + 1 (the selector's step n + 1)
// when the product has a selector; a learned controller moves its macro (reported to the host as a value).
// The instruments have no Auto gain, Delta, In or Mix.
#pragma once
#include "gui_view.hpp"
#include "inst_gui.hpp"
#include "sw/demo_gate.hpp"
#include "sw/denormal.hpp"
#include "sw/license_state.hpp"
#include "sw/param.hpp"
#include "sw/text.hpp"
#include "user_presets.hpp"
#include <clap/clap.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace sw::clapinst {

// optional on Core: void setTempo(double bpm) — the host tempo (tempo-synced effects)
// optional on Core: void setTransport(bool playing, double beats) — the host's transport (an arpeggiator or a gate on the beat)
template <class C, class = void> struct HasTransport : std::false_type {};
template <class C> struct HasTransport<C, std::void_t<decltype(std::declval<C&>().setTransport(true, 0.0))>> : std::true_type {};
template <class C, class = void> struct HasSetTempo : std::false_type {};
template <class C> struct HasSetTempo<C, std::void_t<decltype(std::declval<C&>().setTempo(120.0))>> : std::true_type {};
// optional on Core: modWheel(0..1) (CC 1) and aftertouch(0..1) (channel pressure, CLAP pressure expression)
template <class C, class = void> struct HasModWheel : std::false_type {};
template <class C> struct HasModWheel<C, std::void_t<decltype(std::declval<C&>().modWheel(0.0))>> : std::true_type {};
template <class C, class = void> struct HasAftertouch : std::false_type {};
template <class C> struct HasAftertouch<C, std::void_t<decltype(std::declval<C&>().aftertouch(0.0))>> : std::true_type {};
// optional on Core: beginPatch() / endPatch() around a whole patch (a preset, a restored state): the core changes it without a step
template <class C, class = void> struct HasPatch : std::false_type {};
template <class C> struct HasPatch<C, std::void_t<decltype(std::declval<C&>().beginPatch()), decltype(std::declval<C&>().endPatch())>> : std::true_type {};
template <class T, class = void> struct HasProgram : std::false_type {};
template <class T> struct HasProgram<T, std::void_t<decltype(T::kProgramParam), decltype(T::loadProgram(std::declval<typename T::Core&>(), 0))>> : std::true_type {};
template <class T, class = void> struct HasWarmUp : std::false_type {};
template <class T> struct HasWarmUp<T, std::void_t<decltype(T::warmUp())>> : std::true_type {};
template <class T, class = void> struct HasUi : std::false_type {};
template <class T> struct HasUi<T, std::void_t<decltype(T::ui()), decltype(T::userPresetText(std::declval<const std::vector<double>&>(), std::declval<const sw::presetfile::Meta&>()))>> : std::true_type {};
// optional: static std::string paramModule(int id) — where a parameter sits in a host's generic list ("Layer 1/Filter")
template <class T, class = void> struct HasParamModule : std::false_type {};
template <class T> struct HasParamModule<T, std::void_t<decltype(T::paramModule(0))>> : std::true_type {};
// optional: static constexpr int kMacroCount; static int macroParam(int m) — MIDI learn: a controller (CC) moves a macro (continuous)
template <class T, class = void> struct HasMacros : std::false_type {};
template <class T> struct HasMacros<T, std::void_t<decltype(T::kMacroCount), decltype(T::macroParam(0))>> : std::true_type {};
template <class T, class = void> struct HasActivationServer : std::false_type {};
template <class T> struct HasActivationServer<T, std::void_t<decltype(T::kActivationServer)>> : std::true_type {};
template <class T, class = void> struct HasPresetFiles : std::false_type {};
template <class T> struct HasPresetFiles<T, std::void_t<decltype(T::kPresetVendor), decltype(T::kPresetProduct), decltype(T::factoryPresetCount()),
    decltype(T::factoryPresetName(0)), decltype(T::factoryPresetCategory(0)), decltype(T::factoryPresetValues(0, std::declval<std::vector<double>&>())),
    decltype(T::userPresetValues(std::string_view{}, std::declval<std::vector<double>&>(), std::declval<sw::presetfile::Meta&>(), std::declval<std::string&>()))>>
    : std::true_type {};

// a factory preset by its name (the load key the discovery provider gives), or -1
template <class P> int findFactoryPreset(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < P::factoryPresetCount(); ++i) if (P::factoryPresetName(i) == name) return i;
    return -1;
}

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
    // a host value as it may arrive from a host or a saved session: a number that is not finite reads as the default, anything else is
    // clamped into the parameter's host range (0..1, or 0..steps-1; hostToPlain rounds to a step)
    static double sanitizeHost(int id, double h) {
        if (!std::isfinite(h)) return plainToHost(id, spec(id).def);
        return std::clamp(h, 0.0, stepped(id) ? static_cast<double>(std::max(0, spec(id).numSteps() - 1)) : 1.0);
    }

    explicit Plugin(const clap_host_t* host) : host_(host), host_values_(static_cast<size_t>(numParams())), dirty_(static_cast<size_t>(numParams())), changed_(static_cast<size_t>(numParams()), 0) {
        for (int i = 0; i < numParams(); ++i) { host_values_[static_cast<size_t>(i)].store(plainToHost(i, spec(i).def)); dirty_[static_cast<size_t>(i)].store(true); }
        for (auto& c : ccMacro_) c.store(-1);
        plugin_ = {P::descriptor(), this, init, destroy, activate, deactivate, startProcessing, stopProcessing, reset, process, getExtension, onMainThread};
    }
    const clap_plugin_t* clap() const { return &plugin_; }

private:
    static Plugin* self(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

    void applyPending() {
        const bool patch = patch_.exchange(false);   // a preset or a state from the main thread: one patch change (no step)
        if constexpr (HasPatch<typename P::Core>::value) if (patch) core_.beginPatch();
        for (int i = 0; i < numParams(); ++i)
            if (dirty_[static_cast<size_t>(i)].exchange(false)) core_.setParam(i, hostToPlain(i, host_values_[static_cast<size_t>(i)].load()));
        if constexpr (HasPatch<typename P::Core>::value) if (patch) core_.endPatch();
        (void)patch;
    }
    void handleEvent(const clap_event_header_t* h) {
        if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
        switch (h->type) {
            case CLAP_EVENT_PARAM_VALUE: {
                const auto* ev = reinterpret_cast<const clap_event_param_value_t*>(h);
                if (ev->param_id >= static_cast<clap_id>(numParams())) return;
                const int id = static_cast<int>(ev->param_id);
                const double v = sanitizeHost(id, ev->value);
                host_values_[static_cast<size_t>(id)].store(v);
                core_.setParam(id, hostToPlain(id, v));
                if constexpr (HasProgram<P>::value)
                    if (id == P::kProgramParam) {
                        if constexpr (HasPatch<typename P::Core>::value) core_.beginPatch();
                        P::loadProgram(core_, static_cast<int>(std::lround(v)));
                        if constexpr (HasPatch<typename P::Core>::value) core_.endPatch();
                        syncFromCore();
                        programStep_.store(static_cast<int>(std::lround(v))); programLoads_.fetch_add(1);   // the window shows it
                    }
                return;
            }
            case CLAP_EVENT_NOTE_ON: {
                const auto* ev = reinterpret_cast<const clap_event_note_t*>(h);
                if (ev->key >= 0 && ev->key <= 127) { core_.noteOn(ev->key, ev->velocity, ev->channel < 0 ? 0 : ev->channel, ev->note_id); notes_.fetch_add(1, std::memory_order_relaxed); }
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
                if (st == 0x90 && d2 > 0) { core_.noteOn(d1, d2 / 127.0, ch, -1); notes_.fetch_add(1, std::memory_order_relaxed); }
                else if (st == 0x80 || (st == 0x90 && d2 == 0)) core_.noteOff(d1, ch);
                else if (st == 0xE0) core_.pitchBend(std::clamp(((d2 << 7) | d1) - 8192, -8191, 8191) / 8191.0);
                else if (st == 0xB0 && d1 == 64) core_.sustain(d2 >= 64);
                else if (st == 0xB0 && d1 == 1) { if constexpr (HasModWheel<typename P::Core>::value) core_.modWheel(d2 / 127.0); }
                else if (st == 0xD0) { if constexpr (HasAftertouch<typename P::Core>::value) core_.aftertouch(d1 / 127.0); }
                else if (st == 0xB0 && d1 == 120) core_.allSoundOff();
                else if (st == 0xB0 && d1 == 123) core_.allNotesOff();
                else if (st == 0xB0) midiCc(d1, d2);
                else if (st == 0xC0) programChange(d1);
                return;
            }
            default: return;
        }
    }
    // MIDI Program Change: the selector's step pc + 1 (factory preset pc + 1; 0 = Init is not reachable this way), as when the host sets the
    // selector: one patch, the changed values (and the selector) reported to the host
    void programChange(int pc) {
        if constexpr (HasProgram<P>::value) {
            const int sel = P::kProgramParam, step = pc + 1;
            if (step < 1 || step >= spec(sel).numSteps()) return;
            host_values_[static_cast<size_t>(sel)].store(static_cast<double>(step)); dirty_[static_cast<size_t>(sel)].store(false);
            core_.setParam(sel, hostToPlain(sel, step));
            if constexpr (HasPatch<typename P::Core>::value) core_.beginPatch();
            P::loadProgram(core_, step);
            if constexpr (HasPatch<typename P::Core>::value) core_.endPatch();
            syncFromCore();
            changed_[static_cast<size_t>(sel)] = 1; anyChanged_ = true;
            programStep_.store(step); programLoads_.fetch_add(1);
        }
        (void)pc;
    }
    // controllers a macro may learn: not bank select (0, 32), the mod wheel (1, a modulation source of its own), the pedal (64), the
    // channel mode messages (120..127)
    static bool learnableCc(int cc) { return cc > 1 && cc != 32 && cc != 64 && cc < 120; }
    void midiCc(int cc, int value) {
        if constexpr (HasMacros<P>::value) {
            if (!learnableCc(cc)) return;
            const int l = learn_.load(std::memory_order_relaxed);
            if (l >= 0 && l < P::kMacroCount) {   // learning: this controller is the macro's now (and no other's is)
                for (auto& c : ccMacro_) if (c.load(std::memory_order_relaxed) == l) c.store(-1, std::memory_order_relaxed);
                ccMacro_[static_cast<size_t>(cc)].store(static_cast<int8_t>(l));
                learn_.store(-1);
            }
            const int m = ccMacro_[static_cast<size_t>(cc)].load(std::memory_order_relaxed);
            if (m < 0 || m >= P::kMacroCount) return;
            const int id = P::macroParam(m);
            const double h = sanitizeHost(id, value / 127.0);
            host_values_[static_cast<size_t>(id)].store(h); dirty_[static_cast<size_t>(id)].store(false);
            core_.setParam(id, hostToPlain(id, h));
            changed_[static_cast<size_t>(id)] = 1; anyChanged_ = true;
        }
        (void)cc; (void)value;
    }
    // after a program load: every value the core now holds that differs from the host's goes to the host (emitChanged)
    void syncFromCore() {
        if constexpr (HasProgram<P>::value) {
            for (int i = 0; i < numParams(); ++i) {
                if (i == P::kProgramParam) continue;
                const double h = plainToHost(i, core_.param(i));
                if (std::abs(h - host_values_[static_cast<size_t>(i)].load()) > 1e-9) {
                    host_values_[static_cast<size_t>(i)].store(h); dirty_[static_cast<size_t>(i)].store(false);
                    changed_[static_cast<size_t>(i)] = 1; anyChanged_ = true;
                }
            }
        }
    }
    void emitChanged(const clap_output_events_t* out, uint32_t time) {
        if (!anyChanged_) return;
        anyChanged_ = false;
        for (int i = 0; i < numParams(); ++i) {
            if (!changed_[static_cast<size_t>(i)]) continue;
            changed_[static_cast<size_t>(i)] = 0;
            if (!out) continue;
            clap_event_param_value_t e{}; e.header.size = sizeof(e); e.header.time = time; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            e.header.type = CLAP_EVENT_PARAM_VALUE; e.header.flags = 0;
            e.param_id = static_cast<clap_id>(i); e.cookie = nullptr; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
            e.value = host_values_[static_cast<size_t>(i)].load();
            out->try_push(out, &e.header);
        }
    }
    void emitNoteEnds(const clap_output_events_t* out, uint32_t time) {
        int key = 0, ch = 0, id = -1;
        while (core_.takeEnded(key, ch, id)) {
            if (!out || ch == kScreenChannel) continue;   // a note from the window: the host never sent it
            clap_event_note_t e{}; e.header.size = sizeof(e); e.header.time = time; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            e.header.type = CLAP_EVENT_NOTE_END; e.header.flags = 0;
            e.note_id = id; e.port_index = 0; e.channel = static_cast<int16_t>(ch); e.key = static_cast<int16_t>(key); e.velocity = 0.0;
            out->try_push(out, &e.header);
        }
    }

public:
    // ---- the plug-in window (CLAP gui; main thread). A message from the page and the script that answers it (also the tests' way in).
    std::string guiMessage(const std::string& m) {
        if constexpr (HasUi<P>::value && HasPresetFiles<P>::value) {
            if (!facade_) { facade_ = std::make_unique<GuiFacade>(GuiFacade{*this}); session_ = std::make_unique<instgui::Session<GuiFacade>>(*facade_); }
            return session_->onMessage(m);
        }
        (void)m;
        return {};
    }
    // the page as the window loads it (the boot data: the parameters and their values, the presets, the preset in use, the settings,
    // the licence, the pictures' names, the activation server)
    std::string guiPage() {
        if constexpr (HasUi<P>::value && HasPresetFiles<P>::value) {
            std::string b = "{\"params\":" + instgui::specsJson(P::specs()) + ",\"values\":[";
            for (int i = 0; i < numParams(); ++i) { if (i) b += ","; b += instgui::jsNum(hostToPlain(i, host_values_[static_cast<size_t>(i)].load())); }
            b += "],\"presets\":[";
            for (int i = 0; i < P::factoryPresetCount(); ++i)
                b += (i ? "," : "") + std::string("{\"name\":") + instgui::jsonString(P::factoryPresetName(i)) + ",\"category\":" + instgui::jsonString(P::factoryPresetCategory(i)) + "}";
            b += "],\"current\":" + current_.json() + ",\"settings\":" + settings().json() + ",\"licence\":" + licenceJson("", true) + ",\"assets\":" + instgui::assetsJson(P::ui());
            const char* server = "";
            if constexpr (HasActivationServer<P>::value) server = P::kActivationServer;
            b += ",\"server\":" + instgui::jsonString(server) + ",\"version\":" + instgui::jsonString(P::descriptor()->version) + "}";
            return instgui::page(P::ui(), b);
        }
        return {};
    }
    // the window's size in the host's unit (logical points on macOS, pixels elsewhere): the design times the window size setting
    void guiSize(uint32_t& w, uint32_t& h) {
        if constexpr (HasUi<P>::value) {
            const char* api = gui::platformApi();
            const double k = (api && !std::strcmp(api, "cocoa") ? 1.0 : scale_) * settings().zoomFactor();
            w = static_cast<uint32_t>(P::ui().width * k + 0.5); h = static_cast<uint32_t>(P::ui().height * k + 0.5);
        } else { w = 0; h = 0; }
    }
    // the preset in use, as the window shows it and the state keeps it
    struct Current {
        std::string kind = "init", path, name, category;
        int index = -1;
        static Current factory(int i) { Current c; if (i >= 0) { c.kind = "factory"; c.index = i; } return c; }
        static Current user(const std::string& path, const std::string& name, const std::string& category) { Current c; c.kind = "user"; c.path = path; c.name = name; c.category = category; return c; }
        std::string json() const {
            return "{\"kind\":" + instgui::jsonString(kind) + ",\"index\":" + std::to_string(index) + ",\"path\":" + instgui::jsonString(path)
                 + ",\"name\":" + instgui::jsonString(name) + ",\"category\":" + instgui::jsonString(category) + "}";
        }
        static Current fromJson(std::string_view j) {
            std::map<std::string, std::string> m;
            Current c;
            if (!instgui::readFlatJson(j, m, 4096)) return c;
            if (m["kind"] == "factory") { double i = -1; if (sw::presetfile::parseNumber(m["index"], i) && i >= 0 && i < 100000) c = factory(static_cast<int>(i)); }
            else if (m["kind"] == "user") c = user(sw::presetfile::cleanText(m["path"], 1024), sw::presetfile::cleanText(m["name"], sw::presetfile::kMaxNameChars), sw::presetfile::cleanText(m["category"], 32));
            return c;
        }
    };
    const Current& current() const { return current_; }
    bool demo() const { return demo_.load(); }

private:
    static constexpr int macroCount() { if constexpr (HasMacros<P>::value) return P::kMacroCount; else return 0; }
    static constexpr int kScreenChannel = 16;   // notes played on the window (outside MIDI's 0..15: never mixed up with the host's)
    void setCurrent(const Current& c) { current_ = c; currentChanged_ = true; }
    instgui::Settings& settings() {
        if (!settingsLoaded_) { settingsPath_ = instgui::settingsPath(P::kPresetProduct); settings_ = instgui::loadSettings(settingsPath_); settingsLoaded_ = true; }
        return settings_;
    }
    std::string licenceJson(const std::string& message, bool ok) {
        const std::string pid = P::descriptor()->id;
        const auto v = sw::license::productState(pid.substr(pid.rfind('.') + 1), std::atoi(P::descriptor()->version));
        const bool demo = !v.licensed || sw::license::forcedDemo();
        std::string j = "{\"state\":\"" + std::string(demo ? "demo" : "licensed") + "\",\"enforced\":" + (v.enforced ? "true" : "false")
                      + ",\"machine\":" + instgui::jsonString(sw::license::thisMachine()) + ",\"id\":" + instgui::jsonString(v.licensed && v.enforced ? v.detail : "");
        if (!message.empty()) j += ",\"message\":" + instgui::jsonString(message) + ",\"ok\":" + (ok ? "true" : "false");
        return j + "}";
    }
    void requestFlush() {
        if (!host_) return;
        const auto* hp = static_cast<const clap_host_params_t*>(host_->get_extension(host_, CLAP_EXT_PARAMS));
        if (hp && hp->request_flush) hp->request_flush(host_);
    }
    // GUI thread -> audio thread: gesture begin (0), value (1), gesture end (2); the value is read from host_values_ when it is written
    void guiPush(uint8_t kind, int id) {
        const size_t h = gui_head_.load(std::memory_order_relaxed), t = gui_tail_.load(std::memory_order_acquire);
        if (h - t >= kGuiQueue) return;   // full: dropping a gesture event costs only the host's automation record (the value is in host_values_)
        gui_ops_[h % kGuiQueue] = {kind, id};
        gui_head_.store(h + 1, std::memory_order_release);
        requestFlush();
    }
    void guiDrain(const clap_output_events_t* out, uint32_t time) {
        size_t t = gui_tail_.load(std::memory_order_relaxed);
        const size_t h = gui_head_.load(std::memory_order_acquire);
        while (t != h) {
            const GuiOp op = gui_ops_[t % kGuiQueue]; ++t;
            if (!out || op.id < 0 || op.id >= numParams()) continue;
            if (op.kind == 1) {
                clap_event_param_value_t e{}; e.header.size = sizeof(e); e.header.time = time; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID; e.header.type = CLAP_EVENT_PARAM_VALUE;
                e.header.flags = CLAP_EVENT_IS_LIVE; e.param_id = static_cast<clap_id>(op.id); e.cookie = nullptr; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
                e.value = host_values_[static_cast<size_t>(op.id)].load();
                out->try_push(out, &e.header);
                continue;
            }
            clap_event_param_gesture_t e{}; e.header.size = sizeof(e); e.header.time = time; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID; e.header.flags = CLAP_EVENT_IS_LIVE;
            e.header.type = op.kind == 0 ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END; e.param_id = static_cast<clap_id>(op.id);
            out->try_push(out, &e.header);
        }
        gui_tail_.store(t, std::memory_order_release);
    }
    // notes from the window (GUI thread -> audio thread), played at the start of the next block
    void screenNote(bool on, int key, double vel) {
        const size_t h = note_head_.load(std::memory_order_relaxed), t = note_tail_.load(std::memory_order_acquire);
        if (h - t >= kNoteQueue) return;
        note_ops_[h % kNoteQueue] = {on, static_cast<uint8_t>(key), static_cast<float>(vel)};
        note_head_.store(h + 1, std::memory_order_release);
        if (host_ && host_->request_process) host_->request_process(host_);
    }
    void screenNotes() {
        size_t t = note_tail_.load(std::memory_order_relaxed);
        const size_t h = note_head_.load(std::memory_order_acquire);
        while (t != h) {
            const NoteOp op = note_ops_[t % kNoteQueue]; ++t;
            if (op.on) { core_.noteOn(op.key, op.vel, kScreenChannel, -1); notes_.fetch_add(1, std::memory_order_relaxed); }
            else core_.noteOff(op.key, kScreenChannel);
        }
        note_tail_.store(t, std::memory_order_release);
    }
    // a preset's values from the window: as the host's preset browser loads them (main thread -> host values -> the next block, one patch)
    std::string loadFromWindow(const std::vector<double>& plain, int program, const Current& c) {
        loadValuesFromMainThread(plain, program);
        current_ = c; currentChanged_ = false;
        return instgui::replyScript("loaded", c.json());
    }
    std::string guiCall(const std::string& name, const std::vector<std::string>& a) {
        if constexpr (HasUi<P>::value && HasPresetFiles<P>::value) {
            const std::string folder = sw::userpresets::folder(P::kPresetVendor, P::kPresetProduct);
            if (name == "asset" && a.size() == 1) { int i = -1; if (!instgui::parseInt(a[0], 0, P::ui().assetCount - 1, i)) return {}; return instgui::assetScript(P::ui(), i); }
            if (name == "factory" && a.size() == 1) {   // -1 = Init
                double d = 0;
                if (!sw::presetfile::parseNumber(a[0], d) || d < -1 || d >= P::factoryPresetCount() || d != std::floor(d)) return instgui::replyScript("loaded", "{\"error\":\"no such preset\"}");
                const int i = static_cast<int>(d);
                std::vector<double> plain;
                P::factoryPresetValues(i, plain);   // out of range (-1): Init
                return loadFromWindow(plain, i + 1, Current::factory(i));
            }
            if (name == "user" && a.size() == 1) {
                std::string path, text, err;
                std::vector<double> plain;
                sw::presetfile::Meta meta;
                if (!instgui::unbase64(a[0], path, 4096) || !insideFolder(folder, path)) return instgui::replyScript("loaded", "{\"error\":\"not a preset of the user folder\"}");
                if (!sw::presetfile::readFile(path, text, err) || !P::userPresetValues(text, plain, meta, err) || plain.size() != static_cast<size_t>(numParams()))
                    return instgui::replyScript("loaded", "{\"error\":" + instgui::jsonString(err.empty() ? "the preset could not be read" : err) + "}");
                std::string nm = meta.name;
                if (nm.empty()) nm = sw::presetfile::cleanText(sw::presetfile::pathOf(path).stem().u8string(), sw::presetfile::kMaxNameChars);
                return loadFromWindow(plain, 0, Current::user(path, nm, meta.category));
            }
            if (name == "users" && a.empty()) {
                std::string list = "[";
                int n = 0;
                for (const auto& f : sw::presetfile::listFiles(folder, sw::presetfile::kExtension, 2, 2000)) {
                    std::string text, err;
                    std::vector<double> plain;
                    sw::presetfile::Meta meta;
                    if (!sw::presetfile::readFile(f, text, err) || !P::userPresetValues(text, plain, meta, err)) continue;
                    if (meta.name.empty()) meta.name = sw::presetfile::cleanText(sw::presetfile::pathOf(f).stem().u8string(), sw::presetfile::kMaxNameChars);
                    list += (n++ ? "," : "") + std::string("{\"name\":") + instgui::jsonString(meta.name) + ",\"category\":" + instgui::jsonString(meta.category)
                          + ",\"author\":" + instgui::jsonString(meta.author) + ",\"path\":" + instgui::jsonString(f) + "}";
                }
                return instgui::replyScript("users", "{\"folder\":" + instgui::jsonString(folder) + ",\"list\":" + list + "]}");
            }
            if (name == "save" && a.size() == 1) {
                std::string j;
                std::map<std::string, std::string> m;
                if (!instgui::unbase64(a[0], j, 8192) || !instgui::readFlatJson(j, m)) return instgui::replyScript("saved", "{\"error\":\"bad request\"}");
                sw::presetfile::Meta meta;
                meta.name = sw::presetfile::cleanText(m["name"], sw::presetfile::kMaxNameChars);
                meta.category = sw::presetfile::cleanText(m["category"], sw::presetfile::kMaxTextChars);
                meta.author = sw::presetfile::cleanText(m["author"], sw::presetfile::kMaxTextChars);
                meta.comment = sw::presetfile::cleanText(m["comment"], sw::presetfile::kMaxTextChars);
                if (meta.name.empty()) return instgui::replyScript("saved", "{\"error\":\"a name is needed\"}");
                std::vector<double> plain(static_cast<size_t>(numParams()));
                for (int i = 0; i < numParams(); ++i) plain[static_cast<size_t>(i)] = hostToPlain(i, host_values_[static_cast<size_t>(i)].load());
                const std::string text = P::userPresetText(plain, meta);
                std::string err;
                const bool overwrite = m["overwrite"] == "true";
                const std::string path = sw::userpresets::save(folder, meta.name, text, overwrite, err);
                if (path.empty()) {
                    namespace fs = std::filesystem;
                    std::error_code ec;
                    const bool exists = !overwrite && !folder.empty() && fs::exists(sw::presetfile::pathOf(folder) / fs::u8path(sw::presetfile::safeFileName(meta.name) + "." + sw::presetfile::kExtension), ec);
                    return instgui::replyScript("saved", exists ? std::string("{\"exists\":true}") : "{\"error\":" + instgui::jsonString(err) + "}");
                }
                std::vector<double> back;
                sw::presetfile::Meta bm;
                std::string e2;
                P::userPresetValues(text, back, bm, e2);   // the category as the file keeps it (one of the product's, or none)
                current_ = Current::user(path, meta.name, bm.category); currentChanged_ = false;
                return instgui::replyScript("saved", "{\"ok\":true,\"name\":" + instgui::jsonString(meta.name) + ",\"category\":" + instgui::jsonString(bm.category) + ",\"path\":" + instgui::jsonString(path) + "}");
            }
            if (name == "learn" && a.size() == 1) {   // the next controller moves macro m (-1: stop waiting)
                int m = 0;
                if (a[0] == "-1") learn_.store(-1);
                else if (instgui::parseInt(a[0], 0, macroCount() - 1, m)) learn_.store(m);
                return {};
            }
            if (name == "forget" && a.size() == 1) {
                int m = 0;
                if (!instgui::parseInt(a[0], 0, macroCount() - 1, m)) return {};
                for (auto& c : ccMacro_) if (c.load() == m) c.store(-1);
                if (learn_.load() == m) learn_.store(-1);
                return {};
            }
            if (name == "lic" && a.empty()) return instgui::replyScript("licence", licenceJson("", true));
            if (name == "licfile" && a.size() == 1) {
                std::string text, err;
                if (!instgui::unbase64(a[0], text, sw::license::kMaxBytes)) return instgui::replyScript("licence", licenceJson("not a licence file", false));
                sw::license::License l;
                const std::string pid = P::descriptor()->id;
                const auto st = sw::license::installForProduct(text, pid.substr(pid.rfind('.') + 1), std::atoi(P::descriptor()->version), l, err);
                if (st != sw::license::Status::Valid) return instgui::replyScript("licence", licenceJson(err.empty() ? sw::license::statusText(st) : err, false));
                const auto v = sw::license::productState(pid.substr(pid.rfind('.') + 1), std::atoi(P::descriptor()->version));
                demo_.store(!v.licensed || sw::license::forcedDemo());   // at once: the demo silence stops with the next block
                return instgui::replyScript("licence", licenceJson("activated: " + l.id, true));
            }
            if (name == "set" && a.size() == 2) {
                std::string value;
                if (!instgui::unbase64(a[1], value, 1024)) return {};
                const std::string oldZoom = settings().zoom;
                if (!settings().set(a[0], value)) return {};
                instgui::saveSettings(settingsPath_, settings_);
                if (settings_.zoom != oldZoom && view_ && host_) {   // the window's size follows (the host is asked; it calls set_size)
                    uint32_t w = 0, h = 0; guiSize(w, h);
                    const auto* hg = static_cast<const clap_host_gui_t*>(host_->get_extension(host_, CLAP_EXT_GUI));
                    if (hg && hg->request_resize) hg->request_resize(host_, w, h);
                }
                return {};
            }
        }
        (void)name; (void)a;
        return {};
    }
    // a path the page sends back must be a preset file in the user folder (the page lists them; nothing else is read)
    static bool insideFolder(const std::string& folder, const std::string& path) {
        namespace fs = std::filesystem;
        if (folder.empty() || path.empty()) return false;
        std::error_code ec;
        const fs::path f = fs::weakly_canonical(sw::presetfile::pathOf(folder), ec);
        if (ec) return false;
        const fs::path p = fs::weakly_canonical(sw::presetfile::pathOf(path), ec);
        if (ec || p.extension().u8string() != std::string(".") + sw::presetfile::kExtension) return false;
        auto fi = f.begin(), pi = p.begin();
        for (; fi != f.end(); ++fi, ++pi) { if (pi == p.end() || *fi != *pi) return false; }
        return pi != p.end();
    }
    std::string ccJson() const {   // the controller of each macro (-1 = none)
        std::string j = "[";
        for (int m = 0; m < macroCount(); ++m) {
            int cc = -1;
            for (int i = 0; i < 128 && cc < 0; ++i) if (ccMacro_[static_cast<size_t>(i)].load(std::memory_order_relaxed) == m) cc = i;
            j += (m ? "," : "") + std::to_string(cc);
        }
        return j + "]";
    }
    std::string pollExtra() {   // the preset in use changed outside the window (the host's program, its browser, a state): the window follows
        const uint32_t loads = programLoads_.load();
        if (loads != seenLoads_) { seenLoads_ = loads; const int st = programStep_.load(); current_ = Current::factory(st - 1); currentChanged_ = true; }
        if (!currentChanged_) return {};
        currentChanged_ = false;
        return instgui::replyScript("loaded", current_.json());
    }
    struct GuiFacade {
        Plugin& pl;
        int numParams() { return Plugin::numParams(); }
        double plain(int i) { return hostToPlain(i, pl.host_values_[static_cast<size_t>(i)].load()); }
        void begin(int i) { pl.guiPush(0, i); }
        void end(int i) { pl.guiPush(2, i); }
        void set(int i, double v) {
            pl.host_values_[static_cast<size_t>(i)].store(sanitizeHost(i, plainToHost(i, v)));
            pl.dirty_[static_cast<size_t>(i)].store(true);
            pl.guiPush(1, i);
        }
        void noteOn(int key, double vel) { pl.screenNote(true, key, vel); }
        void noteOff(int key) { pl.screenNote(false, key, 0.0); }
        std::string infoJson() {
            return "{\"bpm\":" + instgui::jsNum(pl.bpm_.load()) + ",\"playing\":" + (pl.playing_.load() ? "true" : "false") + ",\"beat\":" + instgui::jsNum(pl.beat_.load())
                 + ",\"note\":" + std::to_string(pl.notes_.load()) + ",\"held\":" + (pl.held_.load() ? "1" : "0") + ",\"demo\":" + (pl.demo_.load() ? "true" : "false")
                 + ",\"learn\":" + std::to_string(pl.learn_.load()) + ",\"cc\":" + pl.ccJson() + "}";
        }
        std::string call(const std::string& name, const std::vector<std::string>& a) { return pl.guiCall(name, a); }
        std::string pollExtra() { return pl.pollExtra(); }
    };
    static bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating) { return !floating && gui::platformApi() && api && !std::strcmp(api, gui::platformApi()); }
    static bool guiPreferredApi(const clap_plugin_t*, const char** api, bool* floating) { if (!gui::platformApi()) return false; *api = gui::platformApi(); *floating = false; return true; }
    static bool guiCreate(const clap_plugin_t* p, const char* api, bool floating) {
        Plugin* s = self(p);
        if (!guiIsApiSupported(p, api, floating)) return false;
        s->guiDestroyView();
        s->facade_ = std::make_unique<GuiFacade>(GuiFacade{*s});
        s->session_ = std::make_unique<instgui::Session<GuiFacade>>(*s->facade_);
        s->seenLoads_ = s->programLoads_.load(); s->currentChanged_ = false;
        s->view_ = gui::createView(s->guiPage(), [s](const std::string& m) { return s->session_ ? s->session_->onMessage(m) : std::string(); }, s->scale_);
        return s->view_ != nullptr;
    }
    void guiDestroyView() { view_.reset(); session_.reset(); facade_.reset(); }
    static void guiDestroy(const clap_plugin_t* p) { self(p)->guiDestroyView(); }
    static bool guiSetScale(const clap_plugin_t* p, double scale) { self(p)->scale_ = scale > 0.0 ? scale : 1.0; return true; }
    static bool guiGetSize(const clap_plugin_t* p, uint32_t* w, uint32_t* h) { self(p)->guiSize(*w, *h); return *w > 0; }
    static bool guiCanResize(const clap_plugin_t*) { return false; }   // the window's size is a setting of its own (75 .. 130 %)
    static bool guiResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
    static bool guiAdjustSize(const clap_plugin_t* p, uint32_t* w, uint32_t* h) { return guiGetSize(p, w, h); }
    static bool guiSetSize(const clap_plugin_t* p, uint32_t w, uint32_t h) {
        Plugin* s = self(p); uint32_t a = 0, b = 0; s->guiSize(a, b);
        if (w != a || h != b) return false;
        if (s->view_) s->view_->setSize(w, h);
        return true;
    }
    static bool guiSetParent(const clap_plugin_t* p, const clap_window_t* win) {
        Plugin* s = self(p);
        if (!s->view_ || !win || !win->api || std::strcmp(win->api, gui::platformApi()) != 0) return false;
        uint32_t w = 0, h = 0; s->guiSize(w, h); s->view_->setSize(w, h);
        return s->view_->setParent(win->ptr);
    }
    static bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
    static void guiSuggestTitle(const clap_plugin_t*, const char*) {}
    static bool guiShow(const clap_plugin_t* p) { Plugin* s = self(p); if (!s->view_) return false; s->view_->setVisible(true); return true; }
    static bool guiHide(const clap_plugin_t* p) { Plugin* s = self(p); if (!s->view_) return false; s->view_->setVisible(false); return true; }

    // ---- plugin
    static bool init(const clap_plugin_t*) { return true; }
    static void destroy(const clap_plugin_t* p) { delete self(p); }
    static bool activate(const clap_plugin_t* p, double sr, uint32_t, uint32_t maxFrames) {
        Plugin* s = self(p);
        if constexpr (HasWarmUp<P>::value) P::warmUp();
        s->core_.prepare(sr, static_cast<int>(std::max<uint32_t>(1, maxFrames)));
        // the licence (main thread: reads the user's licence folder); without one, the demo silence (sw/demo_gate.hpp)
        const std::string pid = P::descriptor()->id;
        const auto lv = sw::license::productState(pid.substr(pid.rfind('.') + 1), std::atoi(P::descriptor()->version));
        s->demo_.store(!lv.licensed || sw::license::forcedDemo());
        s->gate_.prepare(sr);
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
        {
            const clap_event_transport_t* t = pr->transport;
            const bool playing = t && (t->flags & CLAP_TRANSPORT_IS_PLAYING) && (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE);
            const double beats = playing ? static_cast<double>(t->song_pos_beats) / static_cast<double>(CLAP_BEATTIME_FACTOR) : 0.0;
            if constexpr (HasTransport<typename P::Core>::value) s->core_.setTransport(playing, beats);
            if (t && (t->flags & CLAP_TRANSPORT_HAS_TEMPO)) s->bpm_.store(t->tempo, std::memory_order_relaxed);   // for the window
            s->playing_.store(playing, std::memory_order_relaxed); s->beat_.store(beats, std::memory_order_relaxed);
        }
        s->guiDrain(pr->out_events, 0);
        s->screenNotes();
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
        if (s->demo_.load(std::memory_order_relaxed)) s->gate_.process(ob.data32, static_cast<int>(std::min<uint32_t>(nch, 2)), static_cast<int>(frames));   // unlicensed: the demo silence
        for (uint32_t c = 2; c < nch; ++c) if (ob.data32[c]) std::memset(ob.data32[c], 0, frames * sizeof(float));
        s->emitChanged(pr->out_events, 0);
        s->emitNoteEnds(pr->out_events, frames > 0 ? frames - 1 : 0);
        s->held_.store(s->core_.active(), std::memory_order_relaxed);
        return CLAP_PROCESS_CONTINUE;
    }

    static const void* getExtension(const clap_plugin_t*, const char* id) {
        static const clap_plugin_audio_ports_t ports = {portsCount, portsGet};
        static const clap_plugin_note_ports_t notePorts = {notePortsCount, notePortsGet};
        static const clap_plugin_params_t params = {paramsCount, paramsInfo, paramsValue, paramsToText, paramsFromText, paramsFlush};
        static const clap_plugin_state_t state = {stateSave, stateLoad};
        static const clap_plugin_preset_load_t presetLoad = {presetFromLocation};
        static const clap_plugin_gui_t gui = {guiIsApiSupported, guiPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiResizeHints,
                                              guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide};
        if constexpr (HasUi<P>::value && HasPresetFiles<P>::value)
            if (!std::strcmp(id, CLAP_EXT_GUI) && gui::platformApi()) return &gui;
        if constexpr (HasPresetFiles<P>::value)
            if (!std::strcmp(id, CLAP_EXT_PRESET_LOAD) || !std::strcmp(id, CLAP_EXT_PRESET_LOAD_COMPAT)) return &presetLoad;
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
        if constexpr (HasParamModule<P>::value) std::snprintf(info->module, sizeof(info->module), "%s", P::paramModule(id).c_str());
        else std::snprintf(info->module, sizeof(info->module), "%s", s.id);
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
    static void paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t* out) {
        Plugin* s = self(p);
        for (uint32_t i = 0, n = in ? in->size(in) : 0; i < n; ++i) {
            const clap_event_header_t* h = in->get(in, i);
            if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE) s->handleEvent(h);   // only values here: no notes outside process
        }
        s->applyPending();
        s->guiDrain(out, 0);
        s->emitChanged(out, 0);
    }

    // ---- presets from the host's browser (preset-load): the values go in as a restored state's do (main thread -> host values -> the audio
    // thread applies them at its next block); a factory preset also moves the selector (without loading it twice: only an event loads)
    void loadValuesFromMainThread(const std::vector<double>& plain, int program) {
        patch_.store(true);   // first: the audio thread takes what follows as one patch (values that arrive during its fade wait for it)
        for (int i = 0; i < numParams(); ++i) {
            if constexpr (HasProgram<P>::value)
                if (i == P::kProgramParam) {
                    if (program >= 0) { host_values_[static_cast<size_t>(i)].store(sanitizeHost(i, program)); dirty_[static_cast<size_t>(i)].store(true); }
                    continue;
                }
            host_values_[static_cast<size_t>(i)].store(sanitizeHost(i, plainToHost(i, plain[static_cast<size_t>(i)])));
            dirty_[static_cast<size_t>(i)].store(true);
        }
        if (!active_) applyPending();
        if (host_) {
            const auto* hp = static_cast<const clap_host_params_t*>(host_->get_extension(host_, CLAP_EXT_PARAMS));
            if (hp && hp->rescan) hp->rescan(host_, CLAP_PARAM_RESCAN_VALUES);
        }
    }
    static bool presetFromLocation(const clap_plugin_t* p, uint32_t kind, const char* location, const char* key) {
        if constexpr (HasPresetFiles<P>::value) {
            Plugin* s = self(p);
            std::vector<double> plain;
            std::string err;
            int program = -1;
            if (kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN) {
                const int i = findFactoryPreset<P>(key);
                if (i < 0) err = "no such factory preset";
                else { P::factoryPresetValues(i, plain); program = i + 1; }
            } else if (kind == CLAP_PRESET_DISCOVERY_LOCATION_FILE && location) {
                std::string text;
                sw::presetfile::Meta meta;
                if (sw::presetfile::readFile(location, text, err) && !P::userPresetValues(text, plain, meta, err)) plain.clear();
            } else {
                err = "unknown preset location";
            }
            const auto* hl = s->host_ ? static_cast<const clap_host_preset_load_t*>(s->host_->get_extension(s->host_, CLAP_EXT_PRESET_LOAD)) : nullptr;
            if (!hl && s->host_) hl = static_cast<const clap_host_preset_load_t*>(s->host_->get_extension(s->host_, CLAP_EXT_PRESET_LOAD_COMPAT));
            if (plain.size() != static_cast<size_t>(numParams())) {
                if (err.empty()) err = "the preset could not be read";
                if (hl && hl->on_error) hl->on_error(s->host_, kind, location, key, 0, err.c_str());
                return false;
            }
            s->loadValuesFromMainThread(plain, program);
            if (program > 0) s->setCurrent(Current::factory(program - 1));
            else { sw::presetfile::Meta meta; std::string text, e2; std::vector<double> tmp;
                   if (location && sw::presetfile::readFile(location, text, e2)) P::userPresetValues(text, tmp, meta, e2);
                   s->setCurrent(Current::user(location ? location : "", meta.name, meta.category)); }
            if (hl && hl->loaded) hl->loaded(s->host_, kind, location, kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN ? key : nullptr);
            return true;
        }
        (void)p; (void)kind; (void)location; (void)key;
        return false;
    }

    // ---- state: "SWA1" + count + host values (count-based, so appended params load old states)
    static bool writeAll(const clap_ostream_t* s, const void* d, uint64_t n) {
        const char* c = static_cast<const char*>(d);
        while (n > 0) { const int64_t w = s->write(s, c, n); if (w <= 0) return false; c += w; n -= static_cast<uint64_t>(w); }
        return true;
    }
    static constexpr uint32_t kMaxStateParams = 65536;
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
        auto chunk = [&](const char* tag, const std::string& data) {   // after the values: older versions stop before these
            const uint32_t n = static_cast<uint32_t>(data.size());
            return writeAll(s, tag, 4) && writeAll(s, &n, 4) && writeAll(s, data.data(), n);
        };
        if constexpr (HasPresetFiles<P>::value) if (!chunk("SWNM", self(p)->current_.json())) return false;   // the preset in use (the window shows its name)
        if constexpr (HasMacros<P>::value) {   // MIDI learn
            std::string cc(128, '\0');
            for (size_t i = 0; i < 128; ++i) cc[i] = static_cast<char>(self(p)->ccMacro_[i].load());
            if (!chunk("SWCC", cc)) return false;
        }
        return true;
    }
    static bool stateLoad(const clap_plugin_t* p, const clap_istream_t* s) {
        char magic[4]; uint32_t count = 0;
        if (!readAll(s, magic, 4) || std::memcmp(magic, "SWA1", 4) != 0 || !readAll(s, &count, 4)) return false;
        if (count > kMaxStateParams) return false;   // no product has that many: a damaged or foreign state
        std::vector<double> vals(count);              // read whole before anything changes: a cut state changes nothing
        for (uint32_t i = 0; i < count; ++i) if (!readAll(s, &vals[i], 8)) return false;
        Plugin* pl = self(p);
        {   // optional chunks after the values (a state of an older version has none: the preset in use is then Init, nothing is learned);
            // anything odd about them is ignored, the values count
            Current c;
            std::array<int8_t, 128> cc; cc.fill(-1);
            char tag[4]; uint32_t n = 0;
            for (int k = 0; k < 16 && readAll(s, tag, 4) && readAll(s, &n, 4) && n <= 65536; ++k) {
                std::string d(n, '\0');
                if (n > 0 && !readAll(s, &d[0], n)) break;
                if (!std::memcmp(tag, "SWNM", 4)) c = Current::fromJson(d);
                else if (!std::memcmp(tag, "SWCC", 4) && n == 128)
                    for (size_t i = 0; i < 128; ++i) { const int m = static_cast<int8_t>(d[i]); cc[i] = static_cast<int8_t>(m >= 0 && m < macroCount() && learnableCc(static_cast<int>(i)) ? m : -1); }
            }
            if constexpr (HasPresetFiles<P>::value) pl->setCurrent(c);
            for (size_t i = 0; i < 128; ++i) pl->ccMacro_[i].store(cc[i]);
            pl->learn_.store(-1);
        }
        pl->patch_.store(true);
        for (uint32_t i = 0; i < count && i < static_cast<uint32_t>(numParams()); ++i) {
            pl->host_values_[i].store(sanitizeHost(static_cast<int>(i), vals[i])); pl->dirty_[i].store(true);
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
    std::atomic<bool> patch_{false};   // the dirty values form one patch (a preset or a state from the main thread)
    std::vector<uint8_t> changed_;   // values a program load changed, not yet reported to the host
    bool anyChanged_ = false;
    bool active_ = false;
    sw::DemoGate gate_;   // the demo silence without a licence
    std::atomic<bool> demo_{false};
    // the window
    struct GuiOp { uint8_t kind = 0; int id = 0; };
    static constexpr size_t kGuiQueue = 1024;
    std::array<GuiOp, kGuiQueue> gui_ops_{};
    std::atomic<size_t> gui_head_{0}, gui_tail_{0};
    struct NoteOp { bool on = false; uint8_t key = 0; float vel = 0.f; };
    static constexpr size_t kNoteQueue = 256;
    std::array<NoteOp, kNoteQueue> note_ops_{};
    std::atomic<size_t> note_head_{0}, note_tail_{0};
    std::atomic<double> bpm_{120.0}, beat_{0.0};
    std::atomic<bool> playing_{false}, held_{false};
    std::array<std::atomic<int8_t>, 128> ccMacro_;   // MIDI learn: the macro each controller moves (-1 = none)
    std::atomic<int> learn_{-1};                       // the macro waiting for a controller (-1 = none)
    std::atomic<uint32_t> notes_{0}, programLoads_{0};
    std::atomic<int> programStep_{0};
    uint32_t seenLoads_ = 0;
    Current current_;
    bool currentChanged_ = false;
    instgui::Settings settings_;
    std::string settingsPath_;
    bool settingsLoaded_ = false;
    double scale_ = 1.0;
    std::unique_ptr<GuiFacade> facade_;
    std::unique_ptr<instgui::Session<GuiFacade>> session_;
    std::unique_ptr<gui::View> view_;
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

// ---- the preset-discovery factory: one provider; locations = the factory presets (inside the plug-in) and the user folder (files)
template <class P>
struct PresetDiscovery {
    struct Provider {
        clap_preset_discovery_provider_t clap;
        const clap_preset_discovery_indexer_t* indexer;
    };
    static const clap_preset_discovery_provider_descriptor_t* descriptor() {
        static const std::string id = std::string(P::descriptor()->id) + ".presets";
        static const std::string name = std::string(P::kPresetProduct) + " presets";
        static const clap_preset_discovery_provider_descriptor_t d = {CLAP_VERSION_INIT, id.c_str(), name.c_str(), P::descriptor()->vendor};
        return &d;
    }
    static std::string feature(const std::string& category) {   // LEAD -> lead; SEQ -> sequence (free-form features: the host maps them)
        if (category == "SEQ") return "sequence";
        std::string f = category;
        for (auto& c : f) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return f;
    }
    static bool init(const clap_preset_discovery_provider_t* pr) {
        const Provider* s = static_cast<const Provider*>(pr->provider_data);
        const auto* ix = s->indexer;
        static const std::string typeName = std::string(P::kPresetProduct) + " preset";
        const clap_preset_discovery_filetype_t ft = {typeName.c_str(), "", sw::presetfile::kExtension};
        if (!ix->declare_filetype(ix, &ft)) return false;
        static const std::string factoryName = std::string(P::kPresetProduct) + " factory";
        const clap_preset_discovery_location_t fac = {CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT, factoryName.c_str(), CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr};
        if (!ix->declare_location(ix, &fac)) return false;
        // the user folder: made here (empty) so a host that keeps the declaration finds the first preset the user saves
        const std::string dir = sw::userpresets::folder(P::kPresetVendor, P::kPresetProduct);
        std::error_code ec;
        if (!dir.empty()) std::filesystem::create_directories(sw::presetfile::pathOf(dir), ec);
        if (!dir.empty() && std::filesystem::is_directory(sw::presetfile::pathOf(dir), ec)) {
            static const std::string userName = std::string(P::kPresetProduct) + " user";
            const clap_preset_discovery_location_t usr = {CLAP_PRESET_DISCOVERY_IS_USER_CONTENT, userName.c_str(), CLAP_PRESET_DISCOVERY_LOCATION_FILE, dir.c_str()};
            ix->declare_location(ix, &usr);
        }
        return true;
    }
    static void destroy(const clap_preset_discovery_provider_t* pr) { delete static_cast<const Provider*>(pr->provider_data); }
    static bool getMetadata(const clap_preset_discovery_provider_t*, uint32_t kind, const char* location, const clap_preset_discovery_metadata_receiver_t* r) {
        const clap_universal_plugin_id_t pid = {"clap", P::descriptor()->id};
        if (kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN) {
            for (int i = 0; i < P::factoryPresetCount(); ++i) {
                const std::string& name = P::factoryPresetName(i);
                if (!r->begin_preset(r, name.c_str(), name.c_str())) break;
                r->add_plugin_id(r, &pid);
                r->add_creator(r, P::descriptor()->vendor);
                r->add_feature(r, CLAP_PLUGIN_FEATURE_INSTRUMENT);
                r->add_feature(r, feature(P::factoryPresetCategory(i)).c_str());
            }
            return true;
        }
        if (kind != CLAP_PRESET_DISCOVERY_LOCATION_FILE || !location) return false;
        std::string text, err;
        std::vector<double> plain;
        sw::presetfile::Meta meta;
        if (!sw::presetfile::readFile(location, text, err) || !P::userPresetValues(text, plain, meta, err)) {
            r->on_error(r, 0, err.c_str());
            return false;
        }
        std::string name = meta.name;
        if (name.empty()) name = sw::presetfile::cleanText(sw::presetfile::pathOf(location).stem().u8string(), sw::presetfile::kMaxNameChars);
        if (!r->begin_preset(r, name.c_str(), nullptr)) return true;
        r->add_plugin_id(r, &pid);
        if (!meta.author.empty()) r->add_creator(r, meta.author.c_str());
        if (!meta.comment.empty()) r->set_description(r, meta.comment.c_str());
        r->add_feature(r, CLAP_PLUGIN_FEATURE_INSTRUMENT);
        if (!meta.category.empty()) r->add_feature(r, feature(meta.category).c_str());
        return true;
    }
    static const void* getExtension(const clap_preset_discovery_provider_t*, const char*) { return nullptr; }

    static uint32_t count(const clap_preset_discovery_factory_t*) { return 1; }
    static const clap_preset_discovery_provider_descriptor_t* getDescriptor(const clap_preset_discovery_factory_t*, uint32_t i) { return i == 0 ? descriptor() : nullptr; }
    static const clap_preset_discovery_provider_t* create(const clap_preset_discovery_factory_t*, const clap_preset_discovery_indexer_t* ix, const char* id) {
        if (!ix || !id || std::strcmp(id, descriptor()->id) != 0) return nullptr;
        Provider* s = new Provider{};
        s->indexer = ix;
        s->clap = {descriptor(), s, init, destroy, getMetadata, getExtension};
        return &s->clap;
    }
    static const clap_preset_discovery_factory_t* get() {
        static const clap_preset_discovery_factory_t f = {count, getDescriptor, create};
        return &f;
    }
};

template <class P>
const void* getFactory(const char* id) {
    if (!id) return nullptr;
    if (!std::strcmp(id, CLAP_PLUGIN_FACTORY_ID)) return Factory<P>::get();
    if constexpr (HasPresetFiles<P>::value)
        if (!std::strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID) || !std::strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT)) return PresetDiscovery<P>::get();
    return nullptr;
}

}  // namespace sw::clapinst

#define SW_CLAP_INSTRUMENT_ENTRY(code, Traits)                                                    \
    bool sw_##code##_entry_init(const char*) { return true; }                                     \
    void sw_##code##_entry_deinit() {}                                                            \
    const void* sw_##code##_entry_get_factory(const char* id) { return sw::clapinst::getFactory<Traits>(id); }
