// SW MT01 Loudness — CLAP plugin traits
#include "clap_adapter.hpp"
#include <cstdlib>
#include <cstring>
#include "mt01/mt01.hpp"

namespace {
struct Mt01 {
    using Core = sw::mt01::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::mt01::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 8;   // momentary, short-term, integrated, range, true peak, target, difference, in band (LUFS / LU / dBTP)
    static void readouts(const Core& c, double* o) { o[0] = c.momentary(); o[1] = c.shortTerm(); o[2] = c.integrated(); o[3] = c.range(); o[4] = c.truePeakDb(); o[5] = c.target(); o[6] = c.difference(); o[7] = c.inBand() ? 1.0 : 0.0; }
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static void guiCall(Core& c, const char* n, const char* a) { (void)a; if (!std::strcmp(n, "reset")) c.reset(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_ANALYZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.mt01", "SW MT01 Loudness", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Loudness meter with broadcast presets", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(mt01, Mt01)
