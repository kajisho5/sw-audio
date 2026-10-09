// SW RS07 Mouth Noise — CLAP plugin traits
#include "clap_adapter.hpp"
#include "rs07/rs07.hpp"

namespace {
struct Rs07 {
    using Core = sw::rs07::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::rs07::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_RESTORATION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.rs07", "SW RS07 Mouth Noise", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.1", "Lip smack and mouth click removal in the gaps", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(rs07, Rs07)
