// SW LV13 Live Peq — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include "lv13/lv13.hpp"

namespace {
struct Lv13 {
    using Core = sw::lv13::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv13::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv13", "SW LV13 Live Peq", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.1", "6-band live parametric EQ with RTA suggestions", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv13, Lv13)
