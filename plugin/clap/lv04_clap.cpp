// SW LV04 Safety limiter — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include "lv04/lv04.hpp"

namespace {
struct Lv04 {
    using Core = sw::lv04::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv04::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_LIMITER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv04", "SW LV04 Safety limiter", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Live safety limiter with event log", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv04, Lv04)
