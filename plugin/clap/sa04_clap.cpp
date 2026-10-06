// SW SA04 Transformer — CLAP plugin traits
#include "clap_adapter.hpp"
#include "sa04/sa04.hpp"

namespace {
struct Sa04 {
    using Core = sw::sa04::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::sa04::specs(); }
    static constexpr int kOutputParam = sw::sa04::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DISTORTION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.sa04", "SW SA04 Transformer", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.11.0", "Transformer and preamp colour", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(sa04, Sa04)
