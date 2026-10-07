// SW VO02 Tune Rt — CLAP plugin traits
#include "clap_adapter.hpp"
#include "vo02/vo02.hpp"

namespace {
struct Vo02 {
    using Core = sw::vo02::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::vo02::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::vo02::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_PITCH_CORRECTION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.vo02", "SW VO02 Tune Rt", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Pitch correction for live singing", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(vo02, Vo02)
