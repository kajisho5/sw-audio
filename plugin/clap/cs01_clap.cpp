// SW CS01 Inductor Strip — CLAP plugin traits
#include "clap_adapter.hpp"
#include "cs01/cs01.hpp"

namespace {
struct Cs01 {
    using Core = sw::cs01::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::cs01::specs(); }
    static constexpr int kOutputParam = sw::cs01::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;  // Mix is the compressor's own parallel blend (inside the core)
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_COMPRESSOR, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.cs01", "SW CS01 Inductor Strip", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Inductor channel strip", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(cs01, Cs01)
