// SW GT03 Pedalboard — CLAP plugin traits
#include "clap_adapter.hpp"
#include "gt03/gt03.hpp"

namespace {
struct Gt03 {
    using Core = sw::gt03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::gt03::specs(); }
    static constexpr int kOutputParam = sw::gt03::Output;
    static constexpr int kInParam = sw::gt03::Input;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DISTORTION, CLAP_PLUGIN_FEATURE_MULTI_EFFECTS, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.gt03", "SW GT03 Pedalboard", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Pedalboard with a tuner that is always on", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(gt03, Gt03)
