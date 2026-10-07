// SW GT01 Amp — CLAP plugin traits
#include "clap_adapter.hpp"
#include "gt01/gt01.hpp"

namespace {
struct Gt01 {
    using Core = sw::gt01::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::gt01::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DISTORTION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.gt01", "SW GT01 Amp", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Guitar amplifier head", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(gt01, Gt01)
