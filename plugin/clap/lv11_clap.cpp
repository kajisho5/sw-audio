// SW LV11 Mic Switch — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include "lv11/lv11.hpp"

namespace {
struct Lv11 {
    using Core = sw::lv11::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv11::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv11", "SW LV11 Mic Switch", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Mic on/off, auto mute on silence and cough button", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv11, Lv11)
