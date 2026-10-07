// SW LV09 Hum Cut — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include "lv09/lv09.hpp"

namespace {
struct Lv09 {
    using Core = sw::lv09::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv09::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_RESTORATION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv09", "SW LV09 Hum Cut", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Live mains hum remover that follows drift", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv09, Lv09)
