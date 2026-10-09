// SW LV22 Polarity — CLAP plugin traits (LIVE line); the reference may come in on the second (sidechain) input
#include "clap_adapter.hpp"
#include "lv22/lv22.hpp"

namespace {
struct Lv22 {
    using Core = sw::lv22::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv22::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_ANALYZER, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv22", "SW LV22 Polarity", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Instant in-phase / out-of-phase check", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv22, Lv22)
