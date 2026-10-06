// SW MD05 Rotary — CLAP plugin traits
#include "clap_adapter.hpp"
#include "md05/md05.hpp"

namespace {
struct Md05 {
    using Core = sw::md05::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::md05::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::md05::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_PHASER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.md05", "SW MD05 Rotary", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.11.0", "Rotating speaker simulation", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(md05, Md05)
