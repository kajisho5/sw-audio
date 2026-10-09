// SW LV05 Auto ducker — CLAP plugin traits (LIVE line); the key comes in on the second (sidechain) input
#include "clap_adapter.hpp"
#include "lv05/lv05.hpp"

namespace {
struct Lv05 {
    using Core = sw::lv05::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv05::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 2;   // background gain (dB), voice (key) active (1 / 0)
    static void readouts(const Core& c, double* o) { o[0] = c.gainDb(); o[1] = c.keyActive() ? 1.0 : 0.0; }
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv05", "SW LV05 Auto ducker", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Ducks the program under a voice key", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv05, Lv05)
