// SW LV14 Align — CLAP plugin traits (LIVE line); the measurement reference comes in on the second (sidechain) input
#include "clap_adapter.hpp"
#include "lv14/lv14.hpp"

namespace {
struct Lv14 {
    using Core = sw::lv14::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv14::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv14", "SW LV14 Align", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Speaker time alignment with one-click measurement", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv14, Lv14)
