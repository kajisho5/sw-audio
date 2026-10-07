// SW LV02 Feedback — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include "lv02/lv02.hpp"

namespace {
struct Lv02 {
    using Core = sw::lv02::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv02::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_FILTER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv02", "SW LV02 Feedback", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.12.0", "Howling suppressor with ring out", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv02, Lv02)
