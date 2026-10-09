// SW ST03 Phase Align — CLAP plugin traits
#include "clap_adapter.hpp"
#include <cstring>
#include "st03/st03.hpp"

namespace {
struct St03 {
    using Core = sw::st03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::st03::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::st03::Mix;
    static constexpr int kReadouts = 2;   // Auto align state (0 idle, 1 collecting, 2 ready, 3 done, 4 failed), the correlation it found
    static void readouts(const Core& c, double* o) { o[0] = c.alignState(); o[1] = c.confidence(); }
    // Auto align: collecting runs in the audio thread (4 s), the analysis on the screen's thread when the screen sees "ready"; the three results are handed to the host as parameter writes
    static void guiCall(Core& c, const char* n, const char*) { if (!std::strcmp(n, "autoalign")) c.startAutoAlign(); }
    static bool guiOnGui(const char* n) { return !std::strcmp(n, "analyse"); }
    static void guiCallGui(Core& c, const char*, const char*) { c.analyse(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.st03", "SW ST03 Phase Align", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Time and phase alignment of two microphones", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(st03, St03)
