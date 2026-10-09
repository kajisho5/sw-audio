// SW DY04 Gate — CLAP plugin traits
#include "clap_adapter.hpp"
#include "dy04/dy04.hpp"
#include <cstring>

namespace {
struct Dy04 {
    using Core = sw::dy04::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::dy04::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 4;   // Learn: listening (1 / 0), how far the time is (0 .. 1), the onsets heard, the last result was good (1 / 0)
    static void readouts(const Core& c, double* o) { o[0] = c.learning() ? 1.0 : 0.0; o[1] = c.learnProgress(); o[2] = c.learnOnsets(); o[3] = c.learnedOk() ? 1.0 : 0.0; }
    static void guiCall(Core& c, const char* n, const char*) { if (!std::strcmp(n, "learn")) c.learn(); }   // the screen's "Learn" (on the audio thread)
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_GATE, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.dy04", "SW DY04 Gate", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Gate, expander and ducker", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(dy04, Dy04)
