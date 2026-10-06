// SW SA07 Lo-Fi — CLAP plugin traits
#include "clap_adapter.hpp"
#include "sa07/sa07.hpp"

namespace {
struct Sa07 {
    using Core = sw::sa07::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::sa07::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::sa07::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DISTORTION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.sa07", "SW SA07 Lo-Fi", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.11.0", "Lo-fi degradation with era presets", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(sa07, Sa07)
