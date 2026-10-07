// SW CS04 Modular Strip — CLAP plugin traits
#include "clap_adapter.hpp"
#include "cs04/cs04.hpp"

namespace {
struct Cs04 {
    using Core = sw::cs04::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::cs04::specs(); }
    static constexpr int kOutputParam = -1;  // the EQ module's Output belongs to the module (it moves with the order)
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_COMPRESSOR, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.cs04", "SW CS04 Modular Strip", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Modular channel strip", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(cs04, Cs04)
