// SW LV20 Rta — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include "lv20/lv20.hpp"

namespace {
struct Lv20 {
    using Core = sw::lv20::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv20::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_ANALYZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv20", "SW LV20 Rta", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Real-time analyser with pink noise reference", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv20, Lv20)
