// SW LV01 Voice — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include "lv01/lv01.hpp"

namespace {
struct Lv01 {
    using Core = sw::lv01::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv01::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 11;   // the stage values for Use x Voice (noise depth, low cut Hz, mud, presence, air, comp threshold, ratio, make-up, ceiling), then the compressor's and the limiter's gain (dB)
    static void readouts(const Core& c, double* o) {
        const auto v = c.stage();
        o[0] = v.noiseDepthDb; o[1] = v.lowCutHz; o[2] = v.mudDb; o[3] = v.presenceDb; o[4] = v.airDb; o[5] = v.compThreshDb; o[6] = v.compRatio; o[7] = v.compMakeupDb; o[8] = v.ceilingDb;
        o[9] = c.compGainDb(); o[10] = c.limiterGainDb();
    }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_COMPRESSOR, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv01", "SW LV01 Voice", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "One-knob voice strip: noise, EQ, comp and limit", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv01, Lv01)
