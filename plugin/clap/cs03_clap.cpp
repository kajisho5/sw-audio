// SW CS03 Stepped Strip — CLAP plugin traits
#include "clap_adapter.hpp"
#include "cs03/cs03.hpp"

namespace {
struct Cs03 {
    using Core = sw::cs03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::cs03::specs(); }
    static constexpr int kOutputParam = sw::cs03::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_COMPRESSOR, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.cs03", "SW CS03 Stepped Strip", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.11.0", "Stepped channel strip", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(cs03, Cs03)
