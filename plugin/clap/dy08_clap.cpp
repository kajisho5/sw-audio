// SW DY08 Clean — CLAP plugin traits
#include "clap_adapter.hpp"
#include "dy08/dy08.hpp"

namespace {
struct Dy08 {
    using Core = sw::dy08::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::dy08::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::dy08::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_COMPRESSOR, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.dy08", "SW DY08 Clean", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Transparent digital compressor", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(dy08, Dy08)
