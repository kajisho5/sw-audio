// SW RS06 Dereverb — CLAP plugin traits
#include "clap_adapter.hpp"
#include "rs06/rs06.hpp"

namespace {
struct Rs06 {
    using Core = sw::rs06::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::rs06::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_RESTORATION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.rs06", "SW RS06 Dereverb", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Late reverberation suppression", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(rs06, Rs06)
