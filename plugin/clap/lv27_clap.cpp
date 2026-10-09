// SW LV27 Scene Sync — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include "lv27/lv27.hpp"

namespace {
struct Lv27 {
    using Core = sw::lv27::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv27::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv27", "SW LV27 Scene Sync", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.16.0", "Follows OBS scenes to recall presets", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv27, Lv27)
