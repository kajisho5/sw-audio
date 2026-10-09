// SW RV08 Gated — CLAP plugin traits
#include "clap_adapter.hpp"
#include "rv08/rv08.hpp"
#include <cstring>

namespace {
struct Rv08 {
    using Core = sw::rv08::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::rv08::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::rv08::Mix;
    static constexpr int kUnitParam = sw::rv08::Unit;   // Unit A / B / C
    static constexpr int kReadouts = 4;   // Learn: listening (1 / 0), how far the time is (0 .. 1), the onsets heard, the last result was good (1 / 0)
    static void readouts(const Core& c, double* o) { o[0] = c.learning() ? 1.0 : 0.0; o[1] = c.learnProgress(); o[2] = c.learnOnsets(); o[3] = c.learnedOk() ? 1.0 : 0.0; }
    static void guiCall(Core& c, const char* n, const char*) { if (!std::strcmp(n, "learn")) c.learn(); }   // the screen's "Learn" (on the audio thread)
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_REVERB, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.rv08", "SW RV08 Gated", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Gated reverb", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(rv08, Rv08)
