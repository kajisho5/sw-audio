// SW CS02 Console Strip — CLAP plugin traits
#include "clap_adapter.hpp"
#include "cs02/cs02.hpp"

namespace {
struct Cs02 {
    using Core = sw::cs02::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::cs02::specs(); }
    static constexpr int kOutputParam = -1;  // the fader is the strip's output
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kUnitParam = sw::cs02::Unit;   // Unit A / B / C
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_COMPRESSOR, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.cs02", "SW CS02 Console Strip", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Console channel strip", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(cs02, Cs02)
