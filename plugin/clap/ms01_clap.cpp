// SW MS01 Maximizer — CLAP plugin traits
#include "clap_adapter.hpp"
#include "ms01/ms01.hpp"

namespace {
struct Ms01 {
    using Core = sw::ms01::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::ms01::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_LIMITER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.ms01", "SW MS01 Maximizer", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.12.0", "Maximizer with loudness lock", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(ms01, Ms01)
