// SW ST04 Center — CLAP plugin traits
#include "clap_adapter.hpp"
#include "st04/st04.hpp"

namespace {
struct St04 {
    using Core = sw::st04::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::st04::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.st04", "SW ST04 Center", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Center, width and Haas", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(st04, St04)
