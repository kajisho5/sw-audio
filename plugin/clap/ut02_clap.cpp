// SW UT02 Mono Check — CLAP plugin traits
#include "clap_adapter.hpp"
#include "ut02/ut02.hpp"

namespace {
struct Ut02 {
    using Core = sw::ut02::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::ut02::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.ut02", "SW UT02 Mono Check", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.1", "Mono, side and small-speaker monitoring", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(ut02, Ut02)
