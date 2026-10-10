// SW SA03 Tube — CLAP plugin traits
#include "clap_adapter.hpp"
#include "sa03/sa03.hpp"

namespace {
struct Sa03 {
    using Core = sw::sa03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::sa03::specs(); }
    static constexpr int kOutputParam = sw::sa03::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::sa03::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DISTORTION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.sa03", "SW SA03 Tube", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Tube harmonics with moving bias", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(sa03, Sa03)
