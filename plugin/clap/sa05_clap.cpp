// SW SA05 Exciter — CLAP plugin traits
#include "clap_adapter.hpp"
#include "sa05/sa05.hpp"

namespace {
struct Sa05 {
    using Core = sw::sa05::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::sa05::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::sa05::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DISTORTION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.sa05", "SW SA05 Exciter", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.12.0", "High-frequency exciter with auto fill", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(sa05, Sa05)
