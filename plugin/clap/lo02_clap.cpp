// SW LO02 Sub Gen — CLAP plugin traits
#include "clap_adapter.hpp"
#include "lo02/lo02.hpp"

namespace {
struct Lo02 {
    using Core = sw::lo02::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lo02::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kUnitParam = sw::lo02::Unit;   // Unit A / B / C
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_FILTER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lo02", "SW LO02 Sub Gen", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Phase-locked sub-octave generator", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lo02, Lo02)
