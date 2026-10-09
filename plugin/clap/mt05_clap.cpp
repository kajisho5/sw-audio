// SW MT05 x — CLAP plugin traits
#include "clap_adapter.hpp"
#include "mt05/mt05.hpp"

namespace {
struct Mt05 {
    using Core = sw::mt05::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::mt05::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_ANALYZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.mt05", "SW MT05 Vu Ppm", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "VU and PPM meter", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(mt05, Mt05)
