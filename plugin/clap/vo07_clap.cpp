// SW VO07 Vocal Strip — CLAP plugin traits
#include "clap_adapter.hpp"
#include "vo07/vo07.hpp"

namespace {
struct Vo07 {
    using Core = sw::vo07::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::vo07::specs(); }
    static constexpr int kOutputParam = sw::vo07::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kUnitParam = sw::vo07::Unit;   // Unit A / B / C
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_COMPRESSOR, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.vo07", "SW VO07 Vocal Strip", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Vocal channel strip in the right order", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(vo07, Vo07)
