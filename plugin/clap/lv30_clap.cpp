// SW LV30 Recorder — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include "lv30/lv30.hpp"

namespace {
struct Lv30 {
    using Core = sw::lv30::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv30::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv30", "SW LV30 Recorder", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.12.0", "Always-on backup recording with markers", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv30, Lv30)
