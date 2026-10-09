// SW DY05 De-ess — CLAP plugin traits
#include "clap_adapter.hpp"
#include "dy05/dy05.hpp"

namespace {
struct Dy05 {
    using Core = sw::dy05::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::dy05::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 2;   // gain reduction (dB, <= 0), tracked voice fundamental (Hz)
    static void readouts(const Core& c, double* o) { o[0] = c.gainReductionDb(); o[1] = c.voiceF0(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DEESSER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.dy05", "SW DY05 De-ess", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "De-esser with pitch follow", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(dy05, Dy05)
