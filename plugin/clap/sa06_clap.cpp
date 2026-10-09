// SW SA06 Saturator — CLAP plugin traits
#include "clap_adapter.hpp"
#include "sa06/sa06.hpp"

namespace {
struct Sa06 {
    using Core = sw::sa06::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::sa06::specs(); }
    static constexpr int kOutputParam = sw::sa06::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DISTORTION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.sa06", "SW SA06 Saturator", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.16.0", "Three band saturator with five distortion types", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(sa06, Sa06)
