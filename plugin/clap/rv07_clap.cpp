// SW RV07 Early — CLAP plugin traits
#include "clap_adapter.hpp"
#include "rv07/rv07.hpp"

namespace {
struct Rv07 {
    using Core = sw::rv07::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::rv07::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_REVERB, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.rv07", "SW RV07 Early", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Early reflections", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(rv07, Rv07)
