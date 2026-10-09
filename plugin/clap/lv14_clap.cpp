// SW LV14 Align — CLAP plugin traits (LIVE line); the measurement reference comes in on the second (sidechain) input
#include "clap_adapter.hpp"
#include <cstring>
#include "lv14/lv14.hpp"

namespace {
struct Lv14 {
    using Core = sw::lv14::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv14::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 4;   // distance (m) from the Delay, measure state, found delay (ms), confidence
    static void readouts(const Core& c, double* o) { o[0] = c.distanceM(); o[1] = c.measureState(); o[2] = c.foundMs(); o[3] = c.confidence(); }
    // the Measure button: collecting runs in the audio thread (3 s), the analysis (an FFT) on the screen's thread when the screen sees the state "ready"; the result is handed to the host as the Delay value
    static void guiCall(Core& c, const char* n, const char*) { if (!std::strcmp(n, "measure")) c.startMeasure(); }
    static bool guiOnGui(const char* n) { return !std::strcmp(n, "analyse"); }
    static void guiCallGui(Core& c, const char*, const char*) { c.analyse(); }
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv14", "SW LV14 Align", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Speaker time alignment with one-click measurement", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv14, Lv14)
