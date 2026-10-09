// SW EQ01 Passive — CLAP plugin traits
#include "clap_adapter.hpp"
#include "eq01/eq01.hpp"

namespace {
struct Eq01 {
    using Core = sw::eq01::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::eq01::specs(); }
    static constexpr int kOutputParam = sw::eq01::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.eq01", "SW EQ01 Passive", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.1", "Passive tone shaper with contour", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(eq01, Eq01)
