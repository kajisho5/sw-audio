// SW RV06 Shimmer — CLAP plugin traits
#include "clap_adapter.hpp"
#include "rv06/rv06.hpp"

namespace {
struct Rv06 {
    using Core = sw::rv06::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::rv06::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::rv06::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_REVERB, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.rv06", "SW RV06 Shimmer", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.12.0", "Shimmer reverb", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(rv06, Rv06)
