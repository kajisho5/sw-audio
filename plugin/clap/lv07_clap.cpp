// SW LV07 Speech Agc — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include "lv07/lv07.hpp"

namespace {
struct Lv07 {
    using Core = sw::lv07::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv07::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 2;   // input momentary loudness (LUFS), applied gain (dB)
    static void readouts(const Core& c, double* o) { o[0] = c.loudnessLufs(); o[1] = c.gainDb(); }
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_COMPRESSOR, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv07", "SW LV07 Speech Agc", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Talker leveller for close and distant speakers", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv07, Lv07)
