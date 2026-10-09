// SW DY12 Parallel — CLAP plugin traits
#include "clap_adapter.hpp"
#include "dy12/dy12.hpp"

namespace {
struct Dy12 {
    using Core = sw::dy12::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::dy12::specs(); }
    static constexpr int kOutputParam = sw::dy12::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_COMPRESSOR, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.dy12", "SW DY12 Parallel", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Parallel and upward compressor", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(dy12, Dy12)
