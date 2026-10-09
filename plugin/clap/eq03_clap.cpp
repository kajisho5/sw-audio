// SW EQ03 Mid Shaper — CLAP plugin traits
#include "clap_adapter.hpp"
#include "eq03/eq03.hpp"

namespace {
struct Eq03 {
    using Core = sw::eq03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::eq03::specs(); }
    static constexpr int kOutputParam = sw::eq03::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.eq03", "SW EQ03 Mid Shaper", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.1", "Passive midrange shaper with ride", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(eq03, Eq03)
