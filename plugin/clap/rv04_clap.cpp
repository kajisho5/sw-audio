// SW RV04 Convolution — CLAP plugin traits
#include "clap_adapter.hpp"
#include "rv04/rv04.hpp"

namespace {
struct Rv04 {
    using Core = sw::rv04::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::rv04::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::rv04::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_REVERB, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.rv04", "SW RV04 Convolution", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.1", "Convolution reverb", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(rv04, Rv04)
