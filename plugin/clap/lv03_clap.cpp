// SW LV03 Channel — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include "lv03/lv03.hpp"

namespace {
struct Lv03 {
    using Core = sw::lv03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv03::specs(); }
    static constexpr int kOutputParam = sw::lv03::Out;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 3;   // gate gain, compressor gain, de-esser gain (dB, <= 0)
    static void readouts(const Core& c, double* o) { o[0] = c.gateGainDb(); o[1] = c.compGainDb(); o[2] = c.deessGainDb(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_COMPRESSOR, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv03", "SW LV03 Channel", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.16.0", "Live channel strip with mic presets", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv03, Lv03)
