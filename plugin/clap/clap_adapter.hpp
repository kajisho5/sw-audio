// SW AUDIO — generic CLAP adapter: any product core + the common frame (sw::Shell) -> CLAP plugin.
// A product supplies a traits struct P:
//   using Core = ...;                                   // prepare / setParam / snapToTargets / process / latencySamples
//   static const std::vector<sw::ParamSpec>& specs();   // product parameter table (spec order, never reordered)
//   static constexpr int kOutputParam, kInParam, kMixParam; // routed to the shell (-1 if absent)
//   optional: static constexpr int kUnitParam;              // Unit A / B / C: the shell applies the output stage's gain tolerance
// Optional on Core: void setTempo(double bpm) — receives the host tempo when the transport provides it.
// Optional on Core: void setPlayhead(double seconds, bool playing) — every block; seconds is -1 when the host gives no time.
// Optional on Core: void setTransport(bool playing, double beatsToNextBar) — every block; beatsToNextBar is -1 when the host gives no bar position.
//   static const clap_plugin_descriptor_t* descriptor();
// Host-facing values (spec 共通章 2): continuous = normalized 0..1, stepped = step index.
// Host parameter ids: product params 0..N-1 (their index), the common params (Auto gain, Delta, Bypass) at 0x1000 + 0 / 1 / 2: ids stay stable when a product's list grows. Bypass (CLAP_PARAM_IS_BYPASS, the host's bypass) is the panel's "In" toggle: products with their own In parameter (kInParam >= 0) do not get it.
#pragma once
#include "gui_bridge.hpp"
#include "sw_message.h"
#include "gui_paths.hpp"
#include "gui_view.hpp"
#include "swlink.hpp"
#ifdef SW_SKIN_HEADER
#include SW_SKIN_HEADER   // the product's design (tools/gen_skins.py): kSkinCss, kSkinHtml, kSkinW, kSkinH
#endif
#include "sw/denormal.hpp"
#include "sw/param.hpp"
#include "sw/shell.hpp"
#include "sw/text.hpp"
#include <clap/clap.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/render.h>
#include <clap/ext/tail.h>
#include <clap/ext/track-info.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

namespace sw::clapad {

// cores with hidden state that must be saved with the project (SA02's per-instance seed) implement
// saveExtra(std::vector<uint8_t>&) and loadExtra(const uint8_t*, size_t); it is appended to the state ("SWX1", length, bytes)
template <class C, class = void> struct HasExtraState : std::false_type {};
template <class C>
struct HasExtraState<C, std::void_t<decltype(std::declval<C&>().saveExtra(std::declval<std::vector<uint8_t>&>()))>> : std::true_type {};

// cores that write a parameter themselves (MS05 Ride) implement takeParamWrite(id, plain): bit 0 begin gesture, bit 1 value, bit 2 end gesture
template <class C, class = void> struct HasParamWrite : std::false_type {};
template <class C>
struct HasParamWrite<C, std::void_t<decltype(std::declval<C&>().takeParamWrite(std::declval<int&>(), std::declval<double&>()))>> : std::true_type {};

// optional trait: static constexpr bool kAutoGain = false; -> the product has no Auto gain parameter
template <class P, class = void> struct AutoGainEnabled : std::true_type {};
template <class P> struct AutoGainEnabled<P, std::void_t<decltype(P::kAutoGain)>> : std::bool_constant<P::kAutoGain> {};

// optional trait: static constexpr bool kDelta = false; -> the product has no Delta parameter (meters)
template <class P, class = void> struct DeltaEnabled : std::true_type {};
template <class P> struct DeltaEnabled<P, std::void_t<decltype(P::kDelta)>> : std::bool_constant<P::kDelta> {};

template <class C, class = void> struct HasSetTransport : std::false_type {};
template <class C> struct HasSetTransport<C, std::void_t<decltype(std::declval<C&>().setTransport(false, 0.0))>> : std::true_type {};

// a core that rings on after its input stops reports for how long (seconds, from the current settings; sw/tail.hpp): the host's bounce / freeze / VST3 getTailSamples need it
template <class C, class = void> struct HasTail : std::false_type {};
template <class C> struct HasTail<C, std::void_t<decltype(std::declval<const C&>().tailSeconds())>> : std::true_type {};
// a core that keeps audio (delay lines, reverb tails, convolver histories) and can forget it without allocating: called when the host stops or jumps (clap reset()). A core without it but with a tail is prepared again instead
template <class C, class = void> struct HasReset : std::false_type {};
template <class C> struct HasReset<C, std::void_t<decltype(std::declval<C&>().reset())>> : std::true_type {};
// a core that has work for a background thread (EQ08 / EQ02 Linear: the kernel design, sw/worker.hpp) gets it switched on here, before prepare(); without it (tests, offline) the core does the work itself in process()
// Optional on Core: void setOffline(bool) — the host's render mode (CLAP render extension; a VST3 host's offline process mode arrives the same way through clap-wrapper). Only a core whose output would depend on
// the speed of the bounce (a kernel designed on another thread) has it: offline, it waits for the thread, so the file is the same every time.
template <class C, class = void> struct HasSetOffline : std::false_type {};
template <class C> struct HasSetOffline<C, std::void_t<decltype(std::declval<C&>().setOffline(true))>> : std::true_type {};
template <class C, class = void> struct HasUseWorker : std::false_type {};
template <class C> struct HasUseWorker<C, std::void_t<decltype(std::declval<C&>().useWorker(true))>> : std::true_type {};
// SW Link reference spectrum (plugin/clap/swlink.hpp). The producer (UT03) has static unsigned linkSerial(const Core&) (0: none, otherwise it changes with every new reference) and
// static bool linkBands(const Core&, double* db): both on the audio thread after a block, copied to atomics; the screen's thread shares them with the other instances. The consumer (EQ05 Match)
// has static constexpr const char* kLinkRefFrom = "UT03" and static void linkRefUse(Core&, const double* db): called on the screen's thread when the page sends "linkref"; the screen is told in
// info.link[1] whether a reference is there to take (1 / 0).
template <class P, class = void> struct HasLinkRef : std::false_type {};
template <class P> struct HasLinkRef<P, std::void_t<decltype(P::linkSerial(std::declval<const typename P::Core&>())), decltype(P::linkBands(std::declval<const typename P::Core&>(), std::declval<double*>()))>> : std::true_type {};
template <class P, class = void> struct HasLinkRefUse : std::false_type {};
template <class P> struct HasLinkRefUse<P, std::void_t<decltype(P::kLinkRefFrom), decltype(P::linkRefUse(std::declval<typename P::Core&>(), std::declval<const double*>()))>> : std::true_type {};
// optional trait (LV05 Auto ducker): static const char* linkKeyOf(const Core&) -> the product code of the SW Link instance chosen as the key ("LV01"), null for the host's sidechain input; with
// static constexpr int kLinkKeyReadout = the read-outs slot that gets 1 (the instance is there and plays) or 0. The adapter then gives the core that instance's latest output samples as sidechain 0
// (mono; plugin/clap/swlink.hpp readKey, on the audio thread), and not the host's sidechain.
template <class P, class = void> struct HasLinkKey : std::false_type {};
template <class P> struct HasLinkKey<P, std::void_t<decltype(P::linkKeyOf(std::declval<const typename P::Core&>())), decltype(P::kLinkKeyReadout)>> : std::true_type {};
// optional with HasLinkKey (LO03): static constexpr uint32_t kLinkKeyTag = only an instance whose tag (HasLinkTag) is that counts as the key (LO03: 1 = an instance whose Role is Kick);
// static constexpr bool kLinkKeyOnlyWithoutSidechain = the host's sidechain wins when it carries a signal (LO03: the Bass takes the Kick of the other instance only when no key is connected)
template <class P, class = void> struct LinkKeyTagOf { static constexpr uint32_t value = 0; };
template <class P> struct LinkKeyTagOf<P, std::void_t<decltype(P::kLinkKeyTag)>> { static constexpr uint32_t value = P::kLinkKeyTag; };
template <class P, class = void> struct LinkKeyHostWins { static constexpr bool value = false; };
template <class P> struct LinkKeyHostWins<P, std::void_t<decltype(P::kLinkKeyOnlyWithoutSidechain)>> { static constexpr bool value = P::kLinkKeyOnlyWithoutSidechain; };
// optional trait (LO03): static uint32_t linkTagOf(const Core&) -> what this instance is within its product (the Role: 1 Kick, 2 Bass, 3 Both), published to the others after every block (SW Link tag)
template <class P, class = void> struct HasLinkTag : std::false_type {};
template <class P> struct HasLinkTag<P, std::void_t<decltype(P::linkTagOf(std::declval<const typename P::Core&>()))>> : std::true_type {};
// optional trait (MT05: the 0 VU reference of the session): static constexpr int kLinkSharedParam = the parameter whose value the instances of the product share, static constexpr const char* kLinkSharedFrom
// = the product code ("MT05"); the core has void adoptShared(double) (sets the parameter and hands the value to the host through takeParamWrite). A change the person makes (the host's automation, the screen)
// is published with SW Link (publishShared: atomics only); the other instances take it at their next block; an instance that adopts does not publish. A value equal to the default is no change (an instance
// that has just been made does not overwrite the others'; one that loads another value from a project does).
template <class P, class = void> struct HasLinkShared : std::false_type {};
template <class P> struct HasLinkShared<P, std::void_t<decltype(P::kLinkSharedParam), decltype(P::kLinkSharedFrom)>> : std::true_type {};
template <class C, class = void> struct HasSetPlayhead : std::false_type {};
template <class C> struct HasSetPlayhead<C, std::void_t<decltype(std::declval<C&>().setPlayhead(0.0, false))>> : std::true_type {};

// optional trait: static void guiCall(Core&, const char* name, const char* arg) -> screen buttons (Randomize, Tap ...).
// static constexpr bool kGuiCallOnGuiThread = true -> called on the GUI thread (file I/O), otherwise queued and called on the audio thread
// optional trait: static constexpr int kReadouts = N (<= gui::kMaxReadouts, 160); static void readouts(const Core&, double* out) -> values the core measures (loudness, gain reduction ...) for the screen,
// copied after every block into atomics (the GUI thread reads them; page: SWHOST.update(..., [readouts]))
template <class P, class = void> struct HasReadouts : std::false_type {};
template <class P> struct HasReadouts<P, std::void_t<decltype(P::kReadouts), decltype(P::readouts(std::declval<const typename P::Core&>(), std::declval<double*>()))>> : std::true_type {};

// optional trait: static bool guiOnGui(const char* name); static void guiCallGui(Core&, const char* name, const char* arg) -> a screen button that runs on the GUI thread (file output)
// in a product whose other buttons run on the audio thread (LV23 Export log)
template <class P, class = void> struct HasGuiCallGui : std::false_type {};
template <class P> struct HasGuiCallGui<P, std::void_t<decltype(P::guiOnGui("")), decltype(P::guiCallGui(std::declval<typename P::Core&>(), "", ""))>> : std::true_type {};

template <class P, class = void> struct HasGuiCall : std::false_type {};
template <class P> struct HasGuiCall<P, std::void_t<decltype(P::guiCall(std::declval<typename P::Core&>(), "", ""))>> : std::true_type {};
// optional trait: static void trackInfo(Core&, const char* name /* "" = the host gave none */, uint64_t flags /* CLAP_TRACK_INFO_* */): the host's track information (main thread; the core's side must be thread-safe)
template <class P, class = void> struct HasTrackInfo : std::false_type {};
template <class P> struct HasTrackInfo<P, std::void_t<decltype(P::trackInfo(std::declval<typename P::Core&>(), "", uint64_t(0)))>> : std::true_type {};
// optional trait: static void midi(Core&, int kind, int channel, int d1, int d2): the plug-in takes MIDI on one note input port (audio thread, at the time of the event within the block).
// kind 0 note off (d1 key, d2 velocity 0..127), 1 note on (a note on with velocity 0 arrives as a note off), 2 control change (d1 controller, d2 value), 3 a system real-time byte (d1 = 0xF8 clock ...)
template <class P, class = void> struct HasMidi : std::false_type {};
template <class P> struct HasMidi<P, std::void_t<decltype(P::midi(std::declval<typename P::Core&>(), 0, 0, 0, 0))>> : std::true_type {};
// optional trait: static constexpr int kUnitParam: the product's Unit A / B / C parameter (the shell applies the gain tolerance of the output stage; the core reads its own copy for the rest)
template <class P, class = void> struct UnitParamOf : std::integral_constant<int, -1> {};
template <class P> struct UnitParamOf<P, std::void_t<decltype(P::kUnitParam)>> : std::integral_constant<int, P::kUnitParam> {};
template <class P, class = void> struct GuiCallOnGuiThread : std::false_type {};
template <class P> struct GuiCallOnGuiThread<P, std::void_t<decltype(P::kGuiCallOnGuiThread)>> : std::bool_constant<P::kGuiCallOnGuiThread> {};

template <class C, class = void> struct HasSetTempo : std::false_type {};
template <class C> struct HasSetTempo<C, std::void_t<decltype(std::declval<C&>().setTempo(120.0))>> : std::true_type {};

inline const ParamSpec& autoGainSpec() { static const ParamSpec s{"common.autogain", "Auto gain", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}}; return s; }
inline const ParamSpec& bypassSpec() { static const ParamSpec s{"common.bypass", "Bypass", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}}; return s; }
inline const ParamSpec& deltaSpec() { static const ParamSpec s{"common.delta", "Delta", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}}; return s; }

template <class P>
class Plugin {
public:
    static constexpr bool kHasAutoGain = AutoGainEnabled<P>::value;
    static int numProduct() { return static_cast<int>(P::specs().size()); }
    static int autoGainId() { return kHasAutoGain ? numProduct() : -1; }
    static constexpr bool kHasDelta = DeltaEnabled<P>::value;
    static int deltaId() { return kHasDelta ? numProduct() + (kHasAutoGain ? 1 : 0) : -1; }
    static constexpr bool kHasBypass = P::kInParam < 0;
    static int bypassId() { return kHasBypass ? numProduct() + (kHasAutoGain ? 1 : 0) + (kHasDelta ? 1 : 0) : -1; }
    static int numParams() { return numProduct() + (kHasAutoGain ? 1 : 0) + (kHasDelta ? 1 : 0) + (kHasBypass ? 1 : 0); }
    static const ParamSpec& spec(int id) {
        if (id < numProduct()) return P::specs()[static_cast<size_t>(id)];
        if (id == autoGainId()) return autoGainSpec();
        if (id == bypassId()) return bypassSpec();
        return deltaSpec();
    }
    // CLAP parameter ids (what a host stores in its automation and in its sessions): a product's parameters keep their index; the common switches have ids of their own, 0x1000 + 0 Auto gain,
    // + 1 Delta, + 2 Bypass, whatever the product's number of parameters is - so a parameter added at the end of a product's list (Low lat, Oversample, Unit) never moves them
    static constexpr clap_id kExtraIdBase = 0x1000;
    static clap_id clapId(int index) {
        if (index < numProduct()) return static_cast<clap_id>(index);
        return kExtraIdBase + (index == autoGainId() ? 0u : index == deltaId() ? 1u : 2u);
    }
    static int indexOf(clap_id id) {   // -1: not ours
        if (id < static_cast<clap_id>(numProduct())) return static_cast<int>(id);
        if (id == kExtraIdBase && kHasAutoGain) return autoGainId();
        if (id == kExtraIdBase + 1 && kHasDelta) return deltaId();
        if (id == kExtraIdBase + 2 && kHasBypass) return bypassId();
        return -1;
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
    // a host value as it may arrive from a host or a saved session: a number that is not finite reads as the default, anything else is
    // clamped into the parameter's host range (0..1, or 0..steps-1; hostToPlain rounds to a step)
    static double sanitizeHost(int id, double h) {
        if (!std::isfinite(h)) return plainToHost(id, spec(id).def);
        return std::clamp(h, 0.0, stepped(id) ? static_cast<double>(std::max(0, spec(id).numSteps() - 1)) : 1.0);
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
        else if (id == UnitParamOf<P>::value) shell_.setUnit(static_cast<int>(plain + 0.5));
        else if (id == autoGainId()) shell_.setAutoGain(plain > 0.5);
        else if (kHasDelta && id == deltaId()) shell_.setDelta(plain > 0.5);
        else if (kHasBypass && id == bypassId()) shell_.setIn(plain < 0.5);   // Bypass On = the product is out (10 ms crossfade, the delay stays)
        if (id < numProduct()) shell_.core().setParam(id, plain);
        if constexpr (HasLinkShared<P>::value) { if (id == P::kLinkSharedParam && plain != lastShared_) { lastShared_ = plain; link_.publishShared(plain); } }
    }
    void applyPending() {
        for (int i = 0; i < numParams(); ++i)
            if (dirty_[static_cast<size_t>(i)].exchange(false)) apply(i, hostToPlain(i, host_values_[static_cast<size_t>(i)].load()));
        // snapping needs prepared DSP (e.g. EQ08 builds its kernel): before activation it waits for activate()
        if (active_ && snap_pending_.exchange(false)) { shell_.core().snapToTargets(); shell_.snap(); }
    }
    void handleEvent(const clap_event_header_t* h) {
        if constexpr (HasMidi<P>::value) { if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && handleMidi(h)) return; }
        if (h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) return;
        const auto* ev = reinterpret_cast<const clap_event_param_value_t*>(h);
        const int id = indexOf(ev->param_id);
        if (id < 0) return;
        const double v = sanitizeHost(id, ev->value);
        host_values_[static_cast<size_t>(id)].store(v);
        apply(id, hostToPlain(id, v));
    }

    // ---- MIDI in (the note port): CLAP note events and raw MIDI messages become the trait's midi() calls
    bool handleMidi(const clap_event_header_t* h) {
        if constexpr (HasMidi<P>::value) {
            auto clamp7 = [](double v) { return static_cast<int>(std::clamp(std::lround(v), 0L, 127L)); };
            switch (h->type) {
                case CLAP_EVENT_NOTE_ON: case CLAP_EVENT_NOTE_OFF: {
                    const auto* n = reinterpret_cast<const clap_event_note_t*>(h); if (n->key < 0) return true;   // a note id without a key: not for us
                    const int vel = clamp7(n->velocity * 127.0); const bool on = h->type == CLAP_EVENT_NOTE_ON && vel > 0;
                    P::midi(shell_.core(), on ? 1 : 0, std::max<int>(0, n->channel), std::min<int>(127, n->key), on ? vel : 0); return true;
                }
                case CLAP_EVENT_MIDI: {
                    const auto* m = reinterpret_cast<const clap_event_midi_t*>(h); const int st = m->data[0], ch = st & 0x0F, d1 = m->data[1] & 0x7F, d2 = m->data[2] & 0x7F;
                    if (st >= 0xF8) P::midi(shell_.core(), 3, 0, st, 0);
                    else switch (st & 0xF0) {
                        case 0x80: P::midi(shell_.core(), 0, ch, d1, d2); break;
                        case 0x90: P::midi(shell_.core(), d2 > 0 ? 1 : 0, ch, d1, d2); break;
                        case 0xB0: P::midi(shell_.core(), 2, ch, d1, d2); break;
                        default: break;
                    }
                    return true;
                }
                default: break;
            }
        }
        return false;
    }

    // ---- the plug-in window (CLAP gui extension; the platform view is gui_mac.mm / gui_win.cpp, none on Linux)
    struct GuiFacade {
        Plugin& pl;
        int numParams() { return Plugin::numParams(); }
        double plain(int i) { return hostToPlain(i, pl.host_values_[static_cast<size_t>(i)].load()); }
        void begin(int i) { pl.guiPush(0, i); }
        void end(int i) { pl.guiPush(2, i); }
        void set(int i, double plainValue) {
            const double h = plainToHost(i, plainValue);
            pl.host_values_[static_cast<size_t>(i)].store(h); pl.dirty_[static_cast<size_t>(i)].store(true); pl.guiPush(1, i);
        }
        double latencyMs() { return 1000.0 * pl.shell_.latencySamples() / pl.sr_; }
        double cpu() { return pl.cpu_.load(); }
        double meter(int k) { const float v = pl.peaks_[static_cast<size_t>(k)].load(); return v > 1e-5f ? 20.0 * std::log10(static_cast<double>(v)) : -100.0; }
        void spectrum(double* out) { pl.spec_.compute(pl.sr_, out); }
        void stereo(double* out) { pl.spec_.stereo(out); }
        int numReadouts() { if constexpr (HasReadouts<P>::value) return P::kReadouts; else return 0; }
        double readout(int i) { return pl.ro_[static_cast<size_t>(i)].load(std::memory_order_relaxed); }
        void call(const std::string& name, const std::string& arg) {
            if (name == "linkwatch") { pl.link_watch_.store(arg == "1"); return; }
            if (name == "linkref") { pl.linkRefTake(); return; }
            pl.guiCall(name, arg);
        }
        // SW Link for the screen: [the other instances alive, then (only while a screen part asked for it: "linkwatch 1") the sum of their output spectra, 64 dB values]; returns how many values
        int link(double* out) {
            out[0] = pl.link_.joined() ? pl.link_.peers() : -1;   // -1: this instance is not in the registry (no room, or a layout it does not know)
            if constexpr (HasLinkRefUse<P>::value) { out[1] = out[0] >= 0 && pl.link_.findReference(P::kLinkRefFrom) != 0 ? 1.0 : 0.0; return 2; }   // a reference to take, or not
            if (out[0] < 0 || !pl.link_watch_.load()) return 1;
            pl.link_.others(out + 1); return 1 + gui::kSpecBands;
        }
    };
    // GUI thread -> audio thread: gesture begin (0), value (1), gesture end (2); the value itself is read from host_values_ when the event is written
    void guiPush(uint8_t kind, int id) {
        const size_t h = gui_head_.load(std::memory_order_relaxed), t = gui_tail_.load(std::memory_order_acquire);
        if (h - t >= kGuiQueue) return;   // full: the page keeps sending; dropping a gesture event costs only the host's automation record
        gui_ops_[h % kGuiQueue] = {kind, id}; gui_head_.store(h + 1, std::memory_order_release);
        if (host_) { const auto* hp = static_cast<const clap_host_params_t*>(host_->get_extension(host_, CLAP_EXT_PARAMS)); if (hp && hp->request_flush) hp->request_flush(host_); }
    }
    struct CallOp { char name[16]; char arg[16]; };
    void guiCall(const std::string& name, const std::string& arg) {
        if constexpr (HasGuiCallGui<P>::value) { if (P::guiOnGui(name.c_str())) { P::guiCallGui(shell_.core(), name.c_str(), arg.c_str()); return; } }
        if constexpr (HasGuiCall<P>::value) {
            if constexpr (GuiCallOnGuiThread<P>::value) { P::guiCall(shell_.core(), name.c_str(), arg.c_str()); }   // any length (a piece of a file)
            else {
                if (name.size() >= sizeof(CallOp::name) || arg.size() >= sizeof(CallOp::arg)) return;   // the queue to the audio thread holds short calls only
                const size_t h = call_head_.load(std::memory_order_relaxed), t = call_tail_.load(std::memory_order_acquire);
                if (h - t >= kCallQueue) return;
                CallOp& op = call_ops_[h % kCallQueue]; std::memset(&op, 0, sizeof(op));
                std::memcpy(op.name, name.data(), name.size()); std::memcpy(op.arg, arg.data(), arg.size());
                call_head_.store(h + 1, std::memory_order_release);
                if (host_) { const auto* hp = static_cast<const clap_host_params_t*>(host_->get_extension(host_, CLAP_EXT_PARAMS)); if (hp && hp->request_flush) hp->request_flush(host_); }
            }
        }
    }
    void drainCalls() {
        if constexpr (HasGuiCall<P>::value && !GuiCallOnGuiThread<P>::value) {
            size_t t = call_tail_.load(std::memory_order_relaxed); const size_t h = call_head_.load(std::memory_order_acquire);
            while (t != h) { const CallOp& op = call_ops_[t % kCallQueue]; ++t; P::guiCall(shell_.core(), op.name, op.arg); }
            call_tail_.store(t, std::memory_order_release);
        }
    }
    void guiDrain(const clap_output_events_t* out, uint32_t time) {
        drainCalls();
        size_t t = gui_tail_.load(std::memory_order_relaxed); const size_t h = gui_head_.load(std::memory_order_acquire);
        while (t != h) {
            const GuiOp op = gui_ops_[t % kGuiQueue]; ++t;
            if (!out || op.id < 0 || op.id >= numParams()) continue;
            const int id = op.id;
            if (op.kind == 1) { writeOneValue(out, time, id); continue; }
            clap_event_param_gesture_t e{}; e.header.size = sizeof(e); e.header.time = time; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID; e.header.flags = CLAP_EVENT_IS_LIVE;
            e.header.type = op.kind == 0 ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END; e.param_id = clapId(id); out->try_push(out, &e.header);
        }
        gui_tail_.store(t, std::memory_order_release);
    }
    void writeOneValue(const clap_output_events_t* out, uint32_t time, int id) {
        clap_event_param_value_t e{}; e.header.size = sizeof(e); e.header.time = time; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID; e.header.type = CLAP_EVENT_PARAM_VALUE; e.header.flags = CLAP_EVENT_IS_LIVE;
        e.param_id = clapId(id); e.cookie = nullptr; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = host_values_[static_cast<size_t>(id)].load();
        out->try_push(out, &e.header);
    }
#ifdef SW_SKIN_HEADER
    static constexpr uint32_t kGuiW = gui_assets::kSkinW, kGuiH = gui_assets::kSkinH + 22;   // the design and the strip of level meters under it
    static gui::Skin skin() { return {gui_assets::kSkinCss, gui_assets::kSkinCssSize, gui_assets::kSkinHtml, gui_assets::kSkinHtmlSize}; }
#else
    static constexpr uint32_t kGuiW = 960, kGuiH = 550;
    static gui::Skin skin() { return {}; }
#endif
    static bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating) { return !floating && gui::platformApi() && api && !std::strcmp(api, gui::platformApi()); }
    static bool guiPreferredApi(const clap_plugin_t*, const char** api, bool* floating) { if (!gui::platformApi()) return false; *api = gui::platformApi(); *floating = false; return true; }
    static bool guiCreate(const clap_plugin_t* p, const char* api, bool floating) {
        Plugin* s = self(p);
        if (!guiIsApiSupported(p, api, floating)) return false;
        s->guiDestroyView();
        std::vector<double> init(static_cast<size_t>(numParams())); GuiFacade f{*s}; for (int i = 0; i < numParams(); ++i) init[static_cast<size_t>(i)] = f.plain(i);
        const std::string html = gui::page(gui::codeOf(P::descriptor()->id), P::specs(), kHasAutoGain, kHasDelta, kHasBypass, init, f.latencyMs(), skin());
        s->facade_ = std::make_unique<GuiFacade>(GuiFacade{*s}); const std::string code = gui::codeOf(P::descriptor()->id);
        s->session_ = std::make_unique<gui::Session<GuiFacade>>(*s->facade_, gui::presets::dirFor(code, gui::documentsDir()), code);
        s->view_ = gui::createView(html, [s](const std::string& m) { return s->session_ ? s->session_->onMessage(m) : std::string(); }, s->scale_);
        return s->view_ != nullptr;
    }
    // the window's page messages without a window (sw_message.h): the same facade and session
    static const char* guiMessage(const clap_plugin_t* p, const char* msg) {
        static thread_local std::string reply;
        Plugin* s = self(p); GuiFacade f{*s};
        const std::string code = gui::codeOf(P::descriptor()->id);
        gui::Session<GuiFacade> session(f, gui::presets::dirFor(code, gui::documentsDir()), code);
        reply = session.onMessage(msg ? msg : "");
        return reply.c_str();
    }
    void guiDestroyView() { view_.reset(); session_.reset(); facade_.reset(); link_watch_.store(false); }
    static void guiDestroy(const clap_plugin_t* p) { self(p)->guiDestroyView(); }
    static bool guiSetScale(const clap_plugin_t* p, double scale) { self(p)->scale_ = scale > 0.0 ? scale : 1.0; return true; }
    static bool guiGetSize(const clap_plugin_t* p, uint32_t* w, uint32_t* h) { const char* api = gui::platformApi(); const bool logical = api && !std::strcmp(api, "cocoa"); const double k = logical ? 1.0 : self(p)->scale_; *w = static_cast<uint32_t>(kGuiW * k + 0.5); *h = static_cast<uint32_t>(kGuiH * k + 0.5); return true; }
    static bool guiCanResize(const clap_plugin_t*) { return false; }
    static bool guiResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
    static bool guiAdjustSize(const clap_plugin_t* p, uint32_t* w, uint32_t* h) { return guiGetSize(p, w, h); }
    static bool guiSetSize(const clap_plugin_t* p, uint32_t w, uint32_t h) { uint32_t a, b; guiGetSize(p, &a, &b); return w == a && h == b; }
    static bool guiSetParent(const clap_plugin_t* p, const clap_window_t* win) {
        Plugin* s = self(p); if (!s->view_ || !win || !win->api || std::strcmp(win->api, gui::platformApi()) != 0) return false;
        uint32_t w, h; guiGetSize(p, &w, &h); s->view_->setSize(w, h);
        return s->view_->setParent(win->ptr);
    }
    static bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
    static void guiSuggestTitle(const clap_plugin_t*, const char*) {}
    static bool guiShow(const clap_plugin_t* p) { Plugin* s = self(p); if (!s->view_) return false; s->view_->setVisible(true); return true; }
    static bool guiHide(const clap_plugin_t* p) { Plugin* s = self(p); if (!s->view_) return false; s->view_->setVisible(false); return true; }

    // ---- plugin
    static bool init(const clap_plugin_t* p) { Plugin* s = self(p); const std::string code = gui::codeOf(P::descriptor()->id); s->link_.join(s->spec_.view(), code.c_str()); return true; }
    static void destroy(const clap_plugin_t* p) { delete self(p); }
    static bool activate(const clap_plugin_t* p, double sr, uint32_t, uint32_t maxFrames) {
        Plugin* s = self(p);
        s->maxFrames_ = std::max<uint32_t>(1, maxFrames); for (auto& v : s->scClean_) v.assign(s->maxFrames_, 0.0f); if constexpr (HasLinkKey<P>::value) s->scLink_.assign(s->maxFrames_, 0.0f); if constexpr (HasUseWorker<typename P::Core>::value) s->shell_.core().useWorker(true); s->shell_.prepare(sr, static_cast<int>(s->maxFrames_), 2); s->sr_ = sr; s->link_.setSampleRate(sr); s->pullTrackInfo();
        for (int i = 0; i < numParams(); ++i) s->dirty_[static_cast<size_t>(i)].store(true);
        s->snap_pending_.store(true);
        s->active_ = true;
        s->applyPending();
        s->restart_requested_.store(false);
        s->updateTail();
        return true;
    }
    static void deactivate(const clap_plugin_t* p) { self(p)->active_ = false; }
    static bool startProcessing(const clap_plugin_t*) { return true; }
    static void stopProcessing(const clap_plugin_t*) {}
    // the host stopped or jumped: what rang before must not come out afterwards (a reverb tail from the old position). Audio thread. The Shell forgets its delay line and meters; the core forgets its audio:
    // its own reset(), or (a product with a tail that has none) a new prepare(), which only clears buffers of the size they already have
    static void reset(const clap_plugin_t* p) {
        Plugin* s = self(p);
        if (!s->active_) return;
        s->shell_.reset();
        if constexpr (HasReset<typename P::Core>::value) s->shell_.core().reset();
        else if constexpr (HasTail<typename P::Core>::value) { s->shell_.core().prepare(s->sr_, static_cast<int>(s->maxFrames_)); }
        s->snap_pending_.store(true);
    }

    static clap_process_status process(const clap_plugin_t* p, const clap_process_t* pr) {
        ScopedNoDenormals noDenormals;
        Plugin* s = self(p);
        const auto t0 = std::chrono::steady_clock::now();
        s->applyPending();
        s->guiDrain(pr->out_events, 0);
        if (pr->audio_inputs_count < 1 || pr->audio_outputs_count < 1) return CLAP_PROCESS_ERROR;
        const clap_audio_buffer_t& ib = pr->audio_inputs[0];
        clap_audio_buffer_t& ob = pr->audio_outputs[0];
        const uint32_t nch = std::min(ib.channel_count, ob.channel_count);
        if (!ib.data32 || !ob.data32 || nch == 0) return CLAP_PROCESS_ERROR;
        const uint32_t frames = pr->frames_count;
        if constexpr (HasSetTempo<typename P::Core>::value)
            if (pr->transport && (pr->transport->flags & CLAP_TRANSPORT_HAS_TEMPO)) {
                // 1 .. 1000 bpm goes to the core; a NaN, an infinity, 0 or a negative tempo (a broken tempo track) is "no tempo" (0), which every core reads as such
                // (an infinite tempo gave a NaN output in DL05, MD02, MD03 and MD04; a NaN tempo in DY08: host_smoke --transport)
                const double bpm = pr->transport->tempo;
                s->shell_.core().setTempo(bpm >= 1.0 && bpm <= 1000.0 ? bpm : 0.0);
            }
        if constexpr (HasSetTransport<typename P::Core>::value) {
            // playing, and the beats until the next bar line (-1 when the host does not tell)
            double toBar = -1.0; bool playing = false;
            if (pr->transport) {
                const auto& t = *pr->transport;
                playing = (t.flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
                if ((t.flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) && (t.flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE) && t.tsig_denom > 0)
                    toBar = static_cast<double>(t.bar_start) / CLAP_BEATTIME_FACTOR + t.tsig_num * 4.0 / t.tsig_denom - static_cast<double>(t.song_pos_beats) / CLAP_BEATTIME_FACTOR;
            }
            s->shell_.core().setTransport(playing, toBar);
        }
        if constexpr (HasSetPlayhead<typename P::Core>::value) {
            // host play position in seconds (-1 when the host does not tell) and whether it is playing
            double sec = -1.0; bool playing = false;
            if (pr->transport) {
                playing = (pr->transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
                if (pr->transport->flags & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE) sec = static_cast<double>(pr->transport->song_pos_seconds) / CLAP_SECTIME_FACTOR;
            }
            s->shell_.core().setPlayhead(sec, playing);
        }
        for (uint32_t c = 0; c < nch; ++c)
            if (ob.data32[c] != ib.data32[c]) std::memcpy(ob.data32[c], ib.data32[c], frames * sizeof(float));
        // a NaN, an infinity or an absurd value (more than 120 dB over full scale) in the input is a glitch upstream: left in, it would stay in every recursive filter, envelope and delay line for good (RS05 even looped for ever)
        for (uint32_t c = 0; c < nch; ++c) cleanInput(ob.data32[c], frames);
        s->measure(ob.data32, nch, frames, 0);
        // optional sidechain (host may leave it unconnected)
        const float* scBase[2] = {nullptr, nullptr};
        int scCh = 0;
        if (kSidechain && pr->audio_inputs_count >= 2 && pr->audio_inputs[1].data32 && pr->audio_inputs[1].channel_count > 0) {
            scCh = static_cast<int>(std::min<uint32_t>(2, pr->audio_inputs[1].channel_count));
            for (int c = 0; c < scCh; ++c) {
                scBase[c] = pr->audio_inputs[1].data32[c];
                if (!isClean(scBase[c], frames) && frames <= s->scClean_[static_cast<size_t>(c)].size()) {   // the host's buffer is read-only: the cleaned copy goes to a scratch buffer
                    float* d = s->scClean_[static_cast<size_t>(c)].data(); std::memcpy(d, scBase[c], frames * sizeof(float)); cleanInput(d, frames); scBase[c] = d;
                }
            }
        }
        if constexpr (HasLinkShared<P>::value) {   // a setting another instance of this product published (SW Link): taken here, before the events of the block
            double v;
            if (s->link_.adoptShared(P::kLinkSharedFrom, v) && v != s->lastShared_) { s->lastShared_ = v; s->shell_.core().adoptShared(v); }
        }
        if constexpr (HasLinkKey<P>::value) {   // the key is another instance's output (SW Link), when the screen chose one
            const char* code = P::linkKeyOf(s->shell_.core());
            if constexpr (LinkKeyHostWins<P>::value) {   // a sidechain that carries a signal is the key (a host that leaves an unconnected one silent does not count)
                if (code && scCh > 0) { bool sig = false; for (int c = 0; c < scCh && !sig; ++c) for (uint32_t i = 0; i < frames; ++i) if (scBase[c][i] != 0.0f) { sig = true; break; } if (sig) code = nullptr; }
            }
            if (code) {
                scBase[0] = scBase[1] = nullptr; scCh = 0;
                const bool got = frames <= s->scLink_.size() && s->link_.readKey(code, s->scLink_.data(), static_cast<int>(frames), s->keyState_, LinkKeyTagOf<P>::value);
                if (got) { scBase[0] = s->scLink_.data(); scCh = 1; }
                s->linkKeyFound_.store(got ? 1.0f : 0.0f, std::memory_order_relaxed);
            } else s->linkKeyFound_.store(0.0f, std::memory_order_relaxed);
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
            next = std::min(next, pos + s->maxFrames_);   // never more than the buffers of prepare() hold
            float* chans[2] = {ob.data32[0] + pos, nch > 1 ? ob.data32[1] + pos : ob.data32[0] + pos};
            const float* sc[2] = {scCh > 0 ? scBase[0] + pos : nullptr, scCh > 1 ? scBase[1] + pos : nullptr};
            s->shell_.process(chans, static_cast<int>(nch), static_cast<int>(next - pos), scCh > 0 ? sc : nullptr, scCh);
            if constexpr (HasParamWrite<typename P::Core>::value) s->emitParamWrite(pr->out_events, next - 1);
            pos = next;
        }
        s->measure(ob.data32, nch, frames, 2);
        if constexpr (HasReadouts<P>::value) s->publishReadouts();
        if constexpr (HasLinkKey<P>::value) s->ro_[static_cast<size_t>(P::kLinkKeyReadout)].store(static_cast<double>(s->linkKeyFound_.load(std::memory_order_relaxed)), std::memory_order_relaxed);
        if constexpr (HasLinkRef<P>::value) s->publishLinkRef();
        if constexpr (HasLinkTag<P>::value) s->link_.setTag(P::linkTagOf(s->shell_.core()));
        s->updateTail();
        // a parameter changed the latency (e.g. Lookahead): CLAP only allows that across a restart
        if (s->shell_.core().latencySamples() != s->shell_.latencySamples() && !s->restart_requested_.exchange(true))
            if (s->host_ && s->host_->request_restart) s->host_->request_restart(s->host_);
        if (frames > 0) {   // the CPU figure of the screen: the time of the block over its length
            const double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), len = static_cast<double>(frames) / s->sr_, now = 100.0 * used / len, old = s->cpu_.load();
            s->cpu_.store(old < 0.0 ? now : old + 0.05 * (now - old));
        }
        return CLAP_PROCESS_CONTINUE;
    }

    // peak meters of the screen: block peak with a ~0.3 s fall (slot 0/1 input L/R, 2/3 output L/R)
    void measure(float* const* d, uint32_t nch, uint32_t frames, int slot) {
        if (frames == 0) return;
        if (slot == 2) { spec_.push(d, nch, frames); spec_.pushStereo(d, nch, frames); }
        const float fall = std::exp(-static_cast<float>(frames) / static_cast<float>(0.3 * sr_));
        for (uint32_t c = 0; c < 2; ++c) {
            const float* x = d[std::min(c, nch - 1)]; float pk = 0.f;
            for (uint32_t i = 0; i < frames; ++i) pk = std::max(pk, std::fabs(x[i]));
            auto& a = peaks_[static_cast<size_t>(slot) + c];
            a.store(std::max(pk, a.load() * fall));
        }
    }

    static const void* getExtension(const clap_plugin_t*, const char* id) {
        static const clap_plugin_audio_ports_t ports = {portsCount, portsGet};
        static const clap_plugin_params_t params = {paramsCount, paramsInfo, paramsValue, paramsToText, paramsFromText, paramsFlush};
        static const clap_plugin_state_t state = {stateSave, stateLoad};
        static const clap_plugin_latency_t latency = {latencyGet};
        static const sw_plugin_message_t message = {guiMessage};
        static const clap_plugin_gui_t gui = {guiIsApiSupported, guiPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide};
        if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &ports;
        if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &params;
        if constexpr (HasTail<typename P::Core>::value) { static const clap_plugin_tail_t tailExt = {tailGet}; if (!std::strcmp(id, CLAP_EXT_TAIL)) return &tailExt; }
        if constexpr (HasSetOffline<typename P::Core>::value) { static const clap_plugin_render_t renderExt = {renderRealtimeOnly, renderSet}; if (!std::strcmp(id, CLAP_EXT_RENDER)) return &renderExt; }
        if (!std::strcmp(id, CLAP_EXT_STATE)) return &state;
        if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &latency;
        if (!std::strcmp(id, SW_EXT_MESSAGE)) return &message;
        if constexpr (HasTrackInfo<P>::value) { static const clap_plugin_track_info_t trackInfoExt = {trackInfoChanged}; if (!std::strcmp(id, CLAP_EXT_TRACK_INFO) || !std::strcmp(id, CLAP_EXT_TRACK_INFO_COMPAT)) return &trackInfoExt; }
        if constexpr (HasMidi<P>::value) { static const clap_plugin_note_ports_t notePorts = {notePortsCount, notePortsGet}; if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &notePorts; }
        if (!std::strcmp(id, CLAP_EXT_GUI) && gui::platformApi()) return &gui;
        return nullptr;
    }
    static void onMainThread(const clap_plugin_t*) {}

    // ---- note ports: one MIDI input (the plug-in plays no notes), CLAP notes and MIDI both accepted
    static uint32_t notePortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 1 : 0; }
    static bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info) {
        if (!isInput || index != 0) return false;
        info->id = 0; info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI; info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
        std::snprintf(info->name, sizeof info->name, "MIDI"); return true;
    }

    // ---- the host's track information (CLAP track-info; VST3 hosts through clap-wrapper's IInfoListener): asked for at activation and whenever the host says it changed
    static void trackInfoChanged(const clap_plugin_t* p) { self(p)->pullTrackInfo(); }
    void pullTrackInfo() {
        if constexpr (HasTrackInfo<P>::value) {
            if (!host_) return;
            const auto* h = static_cast<const clap_host_track_info_t*>(host_->get_extension(host_, CLAP_EXT_TRACK_INFO));
            if (!h) h = static_cast<const clap_host_track_info_t*>(host_->get_extension(host_, CLAP_EXT_TRACK_INFO_COMPAT));
            clap_track_info_t ti{}; if (!h || !h->get || !h->get(host_, &ti)) return;
            ti.name[CLAP_NAME_SIZE - 1] = 0;
            P::trackInfo(shell_.core(), (ti.flags & CLAP_TRACK_INFO_HAS_TRACK_NAME) ? ti.name : "", ti.flags);
        }
    }

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
        info->id = clapId(id);
        info->flags = (s.automatable ? CLAP_PARAM_IS_AUTOMATABLE : 0) | (stepped(id) ? CLAP_PARAM_IS_STEPPED : 0) | (kHasBypass && id == bypassId() ? CLAP_PARAM_IS_BYPASS : 0);
        std::snprintf(info->name, sizeof(info->name), "%s", s.name);
        std::snprintf(info->module, sizeof(info->module), "%s", s.id);
        info->min_value = 0;
        info->max_value = stepped(id) ? s.numSteps() - 1 : 1;
        info->default_value = plainToHost(id, s.def);
        return true;
    }
    static bool paramsValue(const clap_plugin_t* p, clap_id id, double* out) {
        const int i = indexOf(id);
        if (i < 0) return false;
        *out = self(p)->host_values_[static_cast<size_t>(i)].load();
        return true;
    }
    static bool paramsToText(const clap_plugin_t*, clap_id id, double value, char* out, uint32_t cap) {
        const int i = indexOf(id);
        if (i < 0 || cap == 0) return false;
        std::snprintf(out, cap, "%s", formatValue(spec(i), hostToPlain(i, value)).c_str());
        return true;
    }
    static bool paramsFromText(const clap_plugin_t*, clap_id id, const char* text, double* out) {
        const int i = indexOf(id);
        if (i < 0 || !text) return false;
        double v = 0;
        if (!parseValue(spec(i), text, v)) return false;
        *out = plainToHost(i, v);
        return true;
    }
    // the core moved a parameter itself: tell the host as a gesture (begin / value / end) so the track's automation can record it
    void emitParamWrite(const clap_output_events_t* out, uint32_t time) {
        for (int guard = 0; guard < 32; ++guard) {   // a core may have several parameters to write (SA07 Era writes three, LV03 Mic sixteen)
            int id = 0; double plain = 0;
            const int f = shell_.core().takeParamWrite(id, plain);
            if (!f) return;
            writeOne(out, time, id, plain, f);
        }
    }
    void writeOne(const clap_output_events_t* out, uint32_t time, int id, double plain, int f) {
        if (!out) return;
        auto gesture = [&](uint16_t type) {
            clap_event_param_gesture_t e{}; e.header.size = sizeof(e); e.header.time = time; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID; e.header.type = type; e.header.flags = CLAP_EVENT_IS_LIVE;
            e.param_id = clapId(id); out->try_push(out, &e.header);
        };
        if (f & 1) gesture(CLAP_EVENT_PARAM_GESTURE_BEGIN);
        if (f & 2) {
            clap_event_param_value_t e{}; e.header.size = sizeof(e); e.header.time = time; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID; e.header.type = CLAP_EVENT_PARAM_VALUE; e.header.flags = CLAP_EVENT_IS_LIVE;
            e.param_id = clapId(id); e.cookie = nullptr; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
            e.value = plainToHost(id, plain);
            host_values_[static_cast<size_t>(id)].store(e.value);
            out->try_push(out, &e.header);
        }
        if (f & 4) gesture(CLAP_EVENT_PARAM_GESTURE_END);
    }
    static void paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t* out) {
        for (uint32_t i = 0, n = in->size(in); i < n; ++i) self(p)->handleEvent(in->get(in, i));
        self(p)->applyPending(); self(p)->guiDrain(out, 0);
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
        if constexpr (HasExtraState<typename P::Core>::value) {
            std::vector<uint8_t> extra; self(p)->shell_.core().saveExtra(extra);
            const char xm[4] = {'S', 'W', 'X', '1'}; const uint32_t len = static_cast<uint32_t>(extra.size());
            if (!writeAll(s, xm, 4) || !writeAll(s, &len, 4) || (len && !writeAll(s, extra.data(), len))) return false;
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
        // The product's parameters come first and the common switches (Auto gain, Delta, Bypass) after them. A state saved by another build of the product - a parameter was added
        // at the end of its list (Low lat, Oversample ...) - has the switches at other positions: they are the last values, the product's are the first ones.
        const uint32_t nProduct = static_cast<uint32_t>(numProduct()), nExtra = static_cast<uint32_t>(numParams()) - nProduct;
        const uint32_t held = count >= nExtra ? count - nExtra : count;   // how many of the values are the product's
        auto put = [&](uint32_t id, double v) { pl->host_values_[id].store(sanitizeHost(static_cast<int>(id), v)); pl->dirty_[id].store(true); };
        for (uint32_t i = 0; i < held && i < nProduct; ++i) put(i, vals[i]);
        for (uint32_t e = 0; e < nExtra && held + e < count; ++e) put(nProduct + e, vals[held + e]);
        if constexpr (HasExtraState<typename P::Core>::value) {   // optional: states saved before the extra block existed end here
            char xm[4]; uint32_t len = 0;
            if (readAll(s, xm, 4) && std::memcmp(xm, "SWX1", 4) == 0 && readAll(s, &len, 4) && len <= (1u << 20)) {
                std::vector<uint8_t> extra(len); if (len == 0 || readAll(s, extra.data(), len)) pl->shell_.core().loadExtra(extra.data(), extra.size());
            }
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
    static bool isClean(const float* x, uint32_t n) { for (uint32_t i = 0; i < n; ++i) if (!(std::fabs(x[i]) <= 1.0e6f)) return false; return true; }   // NaN fails the comparison too
    static void cleanInput(float* x, uint32_t n) { for (uint32_t i = 0; i < n; ++i) if (!(std::fabs(x[i]) <= 1.0e6f)) x[i] = 0.0f; }
    // tail = what the core says it rings on for plus the delay the plug-in reports (the sound leaves that much later); INT32_MAX: it never stops
    void updateTail() {
        if constexpr (HasTail<typename P::Core>::value) {
            const double sec = shell_.core().tailSeconds();
            if (!(sec < 3600.0)) { tail_.store(0x7fffffffu); return; }
            const double samples = std::min(sec, 600.0) * sr_ + static_cast<double>(shell_.latencySamples());
            tail_.store(static_cast<uint32_t>(std::min(samples, 2.0e9)));
        }
    }
    static uint32_t tailGet(const clap_plugin_t* p) { return self(p)->tail_.load(); }
    static bool renderRealtimeOnly(const clap_plugin_t*) { return false; }
    static bool renderSet(const clap_plugin_t* p, clap_plugin_render_mode mode) {
        if constexpr (HasSetOffline<typename P::Core>::value) { self(p)->shell_.core().setOffline(mode == CLAP_RENDER_OFFLINE); return mode == CLAP_RENDER_OFFLINE || mode == CLAP_RENDER_REALTIME; }
        else return false;
    }
    static uint32_t latencyGet(const clap_plugin_t* p) { return static_cast<uint32_t>(self(p)->shell_.latencySamples()); }

    clap_plugin_t plugin_{};
    const clap_host_t* host_ = nullptr;
    Shell<typename P::Core> shell_;
    std::vector<std::atomic<double>> host_values_;
    std::vector<std::atomic<bool>> dirty_;
    std::atomic<bool> snap_pending_{true};
    std::atomic<bool> restart_requested_{false};
    bool active_ = false;
    // plug-in window
    struct GuiOp { uint8_t kind = 0; int id = 0; };
    static constexpr size_t kCallQueue = 64;
    std::array<CallOp, kCallQueue> call_ops_{};
    std::atomic<size_t> call_head_{0}, call_tail_{0};
    static constexpr size_t kGuiQueue = 1024;
    std::array<GuiOp, kGuiQueue> gui_ops_{};
    std::atomic<size_t> gui_head_{0}, gui_tail_{0};
    std::unique_ptr<GuiFacade> facade_;
    std::unique_ptr<gui::Session<GuiFacade>> session_;
    std::unique_ptr<gui::View> view_;
    double scale_ = 1.0, sr_ = 48000.0;
    std::array<std::vector<float>, 2> scClean_;   // room for a cleaned copy of the sidechain (allocated in activate)
    std::vector<float> scLink_;                   // room for the key read from another SW Link instance (allocated in activate)
    link::Member::KeyState keyState_; std::atomic<float> linkKeyFound_{0.0f};
    std::atomic<uint32_t> tail_{0};   // the tail in samples, kept up to date by the audio thread (the host asks from any thread)
    uint32_t maxFrames_ = 4096;   // the largest block activate() promised the buffers for (process() cuts a longer block, which the CLAP rules forbid but a host can still send)
    std::array<std::atomic<float>, 4> peaks_{};
    std::array<std::atomic<double>, gui::kMaxReadouts> ro_{};   // the core's measured values for the screen (trait readouts)
    void publishReadouts() { if constexpr (HasReadouts<P>::value) { static_assert(P::kReadouts <= gui::kMaxReadouts); double v[P::kReadouts > 0 ? P::kReadouts : 1] = {}; P::readouts(shell_.core(), v); for (int i = 0; i < P::kReadouts; ++i) ro_[static_cast<size_t>(i)].store(v[i], std::memory_order_relaxed); } }
    gui::SpectrumTap spec_;   // the output spectrum for the screen (audio thread writes, GUI thread reads)
    // SW Link reference spectrum: the producer's, shared from the audio thread after a block (publishReference only stores atomics: no lock, no allocation); the consumer takes the one it finds (linkRefTake)
    void publishLinkRef() {
        if constexpr (HasLinkRef<P>::value) {
            static_assert(link::kRefBands == 60, "the reference spectrum is sw::BandSpectrum's 60 bands");
            uint32_t ser = static_cast<uint32_t>(P::linkSerial(shell_.core()));
            if (ser == lref_audio_) return;
            float v[link::kRefBands] = {};
            if (ser) { double db[link::kRefBands]; if (P::linkBands(shell_.core(), db)) for (int i = 0; i < link::kRefBands; ++i) v[i] = static_cast<float>(db[i]); else ser = 0; }
            link_.publishReference(ser, v); lref_audio_ = ser;
        }
    }
    void linkRefTake() {
        if constexpr (HasLinkRefUse<P>::value) {
            static_assert(link::kRefBands == 60, "the reference spectrum is sw::BandSpectrum's 60 bands");
            double db[link::kRefBands];
            if (link_.joined() && link_.findReference(P::kLinkRefFrom, db)) P::linkRefUse(shell_.core(), db);
        }
    }
    uint32_t lref_audio_ = 0;
    static double sharedDefault() { if constexpr (HasLinkShared<P>::value) return P::specs()[static_cast<size_t>(P::kLinkSharedParam)].def; else return 0.0; }
    double lastShared_ = sharedDefault();   // the shared setting as this instance last had it (published, adopted or default)
    link::Member link_;       // SW Link: the other SW AUDIO instances of this process read the ring above (declared after it: leaves before the ring is destroyed)
    std::atomic<bool> link_watch_{false};   // a part of the screen wants the others' spectrum (Unmask)
    std::atomic<double> cpu_{-1.0};   // measured: the time of a block over its length, in percent (smoothed)
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
