// SW MD07 Ensemble — CLAP plugin traits
#include "clap_adapter.hpp"
#include "md07/md07.hpp"

namespace {
struct Md07 {
    using Core = sw::md07::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::md07::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::md07::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_CHORUS, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.md07", "SW MD07 Ensemble", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "String-ensemble multi-voice chorus", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(md07, Md07)
