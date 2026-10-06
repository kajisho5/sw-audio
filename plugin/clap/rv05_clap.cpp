// SW RV05 Chamber — CLAP plugin traits
#include "clap_adapter.hpp"
#include "rv05/rv05.hpp"

namespace {
struct Rv05 {
    using Core = sw::rv05::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::rv05::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::rv05::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_REVERB, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.rv05", "SW RV05 Chamber", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.11.0", "Echo chamber", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(rv05, Rv05)
