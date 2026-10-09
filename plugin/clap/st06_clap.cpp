// SW ST06 Mono Low — CLAP plugin traits
#include "clap_adapter.hpp"
#include "st06/st06.hpp"

namespace {
struct St06 {
    using Core = sw::st06::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::st06::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kUnitParam = sw::st06::Unit;   // Unit A / B / C
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.st06", "SW ST06 Mono Low", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Makes the low end mono", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(st06, St06)
