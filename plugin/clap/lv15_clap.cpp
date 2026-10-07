// SW LV15 Auto Mixer — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include "lv15/lv15.hpp"

namespace {
struct Lv15 {
    using Core = sw::lv15::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv15::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_MIXING, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv15", "SW LV15 Auto Mixer", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.12.0", "Gain-sharing automixer for up to 8 mics", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv15, Lv15)
