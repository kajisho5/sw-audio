// SW RV03 Spring — CLAP plugin traits
#include "clap_adapter.hpp"
#include "rv03/rv03.hpp"

namespace {
struct Rv03 {
    using Core = sw::rv03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::rv03::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::rv03::Mix;
    static constexpr int kUnitParam = sw::rv03::Unit;   // Unit A / B / C
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_REVERB, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.rv03", "SW RV03 Spring", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Spring reverb", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(rv03, Rv03)
