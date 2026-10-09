// SW LO03 Low Focus — CLAP plugin traits
#include "clap_adapter.hpp"
#include "lo03/lo03.hpp"

namespace {
struct Lo03 {
    using Core = sw::lo03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lo03::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 1;   // the Focus bell's gain right now (dB, <= 0)
    static void readouts(const Core& c, double* o) { o[0] = c.focusCutDb(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_FILTER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lo03", "SW LO03 Low Focus", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Kick and bass low-end separation", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lo03, Lo03)
