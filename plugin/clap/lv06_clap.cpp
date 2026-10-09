// SW LV06 Stream master — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include "lv06/lv06.hpp"

namespace {
struct Lv06 {
    using Core = sw::lv06::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv06::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 4;   // input short-term loudness (LUFS), auto gain (dB), limiter gain reduction (dB, <= 0), target (LUFS)
    static void readouts(const Core& c, double* o) { o[0] = c.loudnessLufs(); o[1] = c.autoGainDb(); o[2] = c.limiterReductionDb(); o[3] = c.targetLufsNow(); }
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_LIMITER, CLAP_PLUGIN_FEATURE_MASTERING, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv06", "SW LV06 Stream master", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Rides to the platform loudness target", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv06, Lv06)
