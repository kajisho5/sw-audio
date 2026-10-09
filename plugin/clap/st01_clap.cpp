// SW ST01 Imager — CLAP plugin traits
#include "clap_adapter.hpp"
#include "st01/st01.hpp"

namespace {
struct St01 {
    using Core = sw::st01::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::st01::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.st01", "SW ST01 Imager", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Four-band stereo imager", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(st01, St01)
