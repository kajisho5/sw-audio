// SW RS01 Denoise — CLAP plugin traits
#include "clap_adapter.hpp"
#include "rs01/rs01.hpp"

namespace {
struct Rs01 {
    using Core = sw::rs01::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::rs01::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_RESTORATION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.rs01", "SW RS01 Denoise", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Spectral noise suppression", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(rs01, Rs01)
