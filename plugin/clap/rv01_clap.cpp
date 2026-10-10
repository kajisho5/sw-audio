// SW RV01 Hall — CLAP plugin traits
#include "clap_adapter.hpp"
#include "rv01/rv01.hpp"

namespace {
struct Rv01 {
    using Core = sw::rv01::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::rv01::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::rv01::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_REVERB, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.rv01", "SW RV01 Hall", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Algorithmic reverb", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(rv01, Rv01)
