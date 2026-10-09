// SW VO06 Formant — CLAP plugin traits
#include "clap_adapter.hpp"
#include "vo06/vo06.hpp"

namespace {
struct Vo06 {
    using Core = sw::vo06::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::vo06::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::vo06::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_PITCH_SHIFTER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.vo06", "SW VO06 Formant", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.1", "Pitch and formant, timing kept", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(vo06, Vo06)
