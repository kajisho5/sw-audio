// SW EQ09 Tilt — CLAP plugin traits
#include "clap_adapter.hpp"
#include "eq09/eq09.hpp"

namespace {
struct Eq09 {
    using Core = sw::eq09::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::eq09::specs(); }
    static constexpr int kOutputParam = sw::eq09::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.eq09", "SW EQ09 Tilt", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Tilt tone shaper", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(eq09, Eq09)
