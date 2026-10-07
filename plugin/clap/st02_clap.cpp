// SW ST02 Mid Side — CLAP plugin traits
#include "clap_adapter.hpp"
#include "st02/st02.hpp"

namespace {
struct St02 {
    using Core = sw::st02::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::st02::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.st02", "SW ST02 Mid Side", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Mid/side level and tone", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(st02, St02)
