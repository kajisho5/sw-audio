// SW EQ06 Stepped — CLAP plugin traits
#include "clap_adapter.hpp"
#include "eq06/eq06.hpp"

namespace {
struct Eq06 {
    using Core = sw::eq06::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::eq06::specs(); }
    static constexpr int kOutputParam = sw::eq06::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.eq06", "SW EQ06 Stepped", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Proportional stepped EQ", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(eq06, Eq06)
