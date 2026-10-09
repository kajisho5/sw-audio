// SW MS06 Master Chain — CLAP plugin traits
#include "clap_adapter.hpp"
#include "ms06/ms06.hpp"
#include <cstring>

namespace {
struct Ms06 {
    using Core = sw::ms06::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::ms06::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 4;   // the compressor's gain reduction (dB, <= 0), then on the output: short-term LUFS, true peak (dBTP), loudness range (LU)
    static void readouts(const Core& c, double* o) { o[0] = c.compReductionDb(); o[1] = c.outShortTermLufs(); o[2] = c.outTruePeakDb(); o[3] = c.outRangeLu(); }
    static void guiCall(Core& c, const char* n, const char*) { if (!std::strcmp(n, "resetmeters")) c.resetMeters(); }   // the meters of the top line: press them to start again
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_LIMITER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.ms06", "SW MS06 Master Chain", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Master chain with stage gain match", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(ms06, Ms06)
