// SW MS02 True Peak — CLAP plugin traits
#include "clap_adapter.hpp"
#include "ms02/ms02.hpp"

namespace {
struct Ms02 {
    using Core = sw::ms02::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::ms02::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_LIMITER, CLAP_PLUGIN_FEATURE_MASTERING, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.ms02", "SW MS02 True Peak", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "True peak safety limiter", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(ms02, Ms02)
