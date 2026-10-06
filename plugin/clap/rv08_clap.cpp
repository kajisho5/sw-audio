// SW RV08 Gated — CLAP plugin traits
#include "clap_adapter.hpp"
#include "rv08/rv08.hpp"

namespace {
struct Rv08 {
    using Core = sw::rv08::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::rv08::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::rv08::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_REVERB, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.rv08", "SW RV08 Gated", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.11.0", "Gated reverb", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(rv08, Rv08)
