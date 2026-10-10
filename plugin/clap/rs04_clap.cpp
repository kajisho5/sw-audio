// SW RS04 Declick — CLAP plugin traits
#include "clap_adapter.hpp"
#include "rs04/rs04.hpp"

namespace {
struct Rs04 {
    using Core = sw::rs04::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::rs04::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_RESTORATION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.rs04", "SW RS04 Declick", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Click and crackle removal", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(rs04, Rs04)
