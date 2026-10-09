// SW ST03 Phase Align — CLAP plugin traits
#include "clap_adapter.hpp"
#include "st03/st03.hpp"

namespace {
struct St03 {
    using Core = sw::st03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::st03::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::st03::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.st03", "SW ST03 Phase Align", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Time and phase alignment of two microphones", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(st03, St03)
