// SW MT02 Spectrum — CLAP plugin traits
#include "clap_adapter.hpp"
#include "mt02/mt02.hpp"

namespace {
struct Mt02 {
    using Core = sw::mt02::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::mt02::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_ANALYZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.mt02", "SW MT02 Spectrum", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Spectrum analyser", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(mt02, Mt02)
