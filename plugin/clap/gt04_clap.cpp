// SW GT04 Bass Amp — CLAP plugin traits
#include "clap_adapter.hpp"
#include "gt04/gt04.hpp"

namespace {
struct Gt04 {
    using Core = sw::gt04::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::gt04::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kUnitParam = sw::gt04::Unit;   // Unit A / B / C
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DISTORTION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.gt04", "SW GT04 Bass Amp", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Bass amplifier with DI blend", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(gt04, Gt04)
