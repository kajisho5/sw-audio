// SW EQ04 Inductor — CLAP plugin traits
#include "clap_adapter.hpp"
#include "eq04/eq04.hpp"

namespace {
struct Eq04 {
    using Core = sw::eq04::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::eq04::specs(); }
    static constexpr int kOutputParam = sw::eq04::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.eq04", "SW EQ04 Inductor", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.11.0", "Inductor console EQ with iron", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(eq04, Eq04)
