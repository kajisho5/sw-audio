// SW SA08 Bitcrush — CLAP plugin traits
#include "clap_adapter.hpp"
#include "sa08/sa08.hpp"

namespace {
struct Sa08 {
    using Core = sw::sa08::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::sa08::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::sa08::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DISTORTION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.sa08", "SW SA08 Bitcrush", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Bit and sample-rate reduction", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(sa08, Sa08)
