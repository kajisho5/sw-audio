// SW LV18 Pop Guard — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include "lv18/lv18.hpp"

namespace {
struct Lv18 {
    using Core = sw::lv18::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv18::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_RESTORATION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv18", "SW LV18 Pop Guard", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Catches plug pops, wind, handling and plosive noise", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv18, Lv18)
