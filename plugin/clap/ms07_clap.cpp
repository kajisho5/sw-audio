// SW MS07 Dither — CLAP plugin traits
#include "clap_adapter.hpp"
#include "ms07/ms07.hpp"
#include <cstring>

namespace {
struct Ms07 {
    using Core = sw::ms07::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::ms07::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;  // level changes after quantization would break the bit depth
    static constexpr int kReadouts = 3;       // Truncation check: listening (1 / 0), how far the 5 s of signal are (0 .. 1), the grid found (8 / 12 / 16 / 20 / 24, 32 = float, 0 = none yet)
    static void readouts(const Core& c, double* o) { o[0] = c.checking() ? 1.0 : 0.0; o[1] = c.checkProgress(); o[2] = c.checkedBits(); }
    static void guiCall(Core& c, const char* n, const char*) { if (!std::strcmp(n, "check")) c.check(); }   // the screen's "Truncation check" (on the audio thread)
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_MASTERING, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.ms07", "SW MS07 Dither", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.16.0", "Dither and requantizer", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(ms07, Ms07)
