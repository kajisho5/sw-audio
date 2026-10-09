// SW SA01 Tape — CLAP plugin traits
#include "clap_adapter.hpp"
#include "sa01/sa01.hpp"

namespace {
struct Sa01 {
    using Core = sw::sa01::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::sa01::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kUnitParam = sw::sa01::Unit;   // Unit A / B / C
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DISTORTION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.sa01", "SW SA01 Tape", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Tape saturation, head bump and wow", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(sa01, Sa01)
