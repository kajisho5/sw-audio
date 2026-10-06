// SW DY06 Vari-Mu — CLAP plugin traits
#include "clap_adapter.hpp"
#include "dy06/dy06.hpp"

namespace {
struct Dy06 {
    using Core = sw::dy06::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::dy06::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::dy06::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_COMPRESSOR, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.dy06", "SW DY06 Vari-Mu", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.11.0", "Variable-mu tube compressor", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(dy06, Dy06)
