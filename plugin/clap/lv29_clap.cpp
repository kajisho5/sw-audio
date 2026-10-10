// SW LV29 Interp Mix — CLAP plugin traits (LIVE line); the interpreter comes in on the second (sidechain) input
#include "clap_adapter.hpp"
#include "lv29/lv29.hpp"

namespace {
struct Lv29 {
    using Core = sw::lv29::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv29::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 3;   // floor gain (dB), interpreter speaking (1 / 0), the interpreter's source found in SW Link (1 / 0; the adapter writes it)
    static void readouts(const Core& c, double* o) { o[0] = c.floorGainDb(); o[1] = c.interpreterSpeaking() ? 1.0 : 0.0; o[2] = 0.0; }
    // SW Link: Interp from = the output of the instance of one LIVE product replaces the sidechain
    static const char* linkKeyOf(const Core& c) { return c.interpSource(); }
    static constexpr int kLinkKeyReadout = 2;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_MIXING, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv29", "SW LV29 Interp Mix", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.16.0", "Venue and interpreter mix that ducks the floor", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv29, Lv29)
