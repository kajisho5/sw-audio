// SW SA02 Console Sum — CLAP plugin traits
#include "clap_adapter.hpp"
#include "sa02/sa02.hpp"

namespace {
struct Sa02 {
    using Core = sw::sa02::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::sa02::specs(); }
    static constexpr int kOutputParam = sw::sa02::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DISTORTION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.sa02", "SW SA02 Console Sum", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.1", "Console summing colour", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(sa02, Sa02)
