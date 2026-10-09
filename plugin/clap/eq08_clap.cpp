// SW EQ08 Linear — CLAP plugin traits
#include "clap_adapter.hpp"
#include "eq08/eq08.hpp"

namespace {
struct Eq08 {
    using Core = sw::eq08::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::eq08::specs(); }
    static constexpr int kOutputParam = sw::eq08::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.eq08", "SW EQ08 Linear", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.16.0", "Linear phase EQ with pre-ring guard", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(eq08, Eq08)
