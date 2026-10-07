// SW LV26 Mono — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include "lv26/lv26.hpp"

namespace {
struct Lv26 {
    using Core = sw::lv26::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv26::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv26", "SW LV26 Mono", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Mono-compatibility safety with auto phase fix", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv26, Lv26)
