// SW UT03 Reference — CLAP plugin traits
#include "clap_adapter.hpp"
#include "ut03/ut03.hpp"

namespace {
struct Ut03 {
    using Core = sw::ut03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::ut03::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_ANALYZER, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.ut03", "SW UT03 Reference", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.1", "A/B against reference tracks at matched loudness", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(ut03, Ut03)
