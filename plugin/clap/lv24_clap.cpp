// SW LV24 Live Reverb — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include "lv24/lv24.hpp"

namespace {
struct Lv24 {
    using Core = sw::lv24::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv24::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::lv24::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_REVERB, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv24", "SW LV24 Live Reverb", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.1", "Low-load reverb that ducks under speech", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv24, Lv24)
