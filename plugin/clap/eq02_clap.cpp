// SW EQ02 Surgical — CLAP plugin traits
#include "clap_adapter.hpp"
#include "eq02/eq02.hpp"

namespace {
struct Eq02 {
    using Core = sw::eq02::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::eq02::specs(); }
    static constexpr int kOutputParam = sw::eq02::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.eq02", "SW EQ02 Surgical", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "24-band surgical and dynamic EQ", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(eq02, Eq02)
