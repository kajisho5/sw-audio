// SW MS04 Clipper — CLAP plugin traits
#include "clap_adapter.hpp"
#include "ms04/ms04.hpp"

namespace {
struct Ms04 {
    using Core = sw::ms04::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::ms04::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::ms04::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DISTORTION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.ms04", "SW MS04 Clipper", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.1", "Oversampled clipper with morphing knee", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(ms04, Ms04)
