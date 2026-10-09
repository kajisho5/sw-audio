// SW ST05 Phones — CLAP plugin traits
#include "clap_adapter.hpp"
#include "st05/st05.hpp"

namespace {
struct St05 {
    using Core = sw::st05::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::st05::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.st05", "SW ST05 Phones", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.16.0", "Loudspeakers in a room on headphones", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(st05, St05)
