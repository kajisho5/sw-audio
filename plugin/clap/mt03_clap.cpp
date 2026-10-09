// SW MT03 Spectrogram — CLAP plugin traits
#include "clap_adapter.hpp"
#include "mt03/mt03.hpp"

namespace {
struct Mt03 {
    using Core = sw::mt03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::mt03::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_ANALYZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.mt03", "SW MT03 Spectrogram", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.16.0", "Scrolling spectrogram", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(mt03, Mt03)
