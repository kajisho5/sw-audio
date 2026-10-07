// SW LV10 Voice Fx — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include "lv10/lv10.hpp"

namespace {
struct Lv10 {
    using Core = sw::lv10::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv10::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::lv10::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_PITCH_SHIFTER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv10", "SW LV10 Voice Fx", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Live voice changer", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv10, Lv10)
