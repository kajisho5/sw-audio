// SW EQ07 Dynamic — CLAP plugin traits
#include "clap_adapter.hpp"
#include "eq07/eq07.hpp"
#include <cstring>

namespace {
struct Eq07 {
    using Core = sw::eq07::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::eq07::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 2;   // Auto thresh: listening (1 / 0), how far it is (0 .. 1)
    static void readouts(const Core& c, double* o) { o[0] = c.learning() ? 1.0 : 0.0; o[1] = c.learnProgress(); }
    static void guiCall(Core& c, const char* n, const char*) { if (!std::strcmp(n, "learn")) c.learnThresholds(); }   // the screen's "Auto thresh" (on the audio thread)
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.eq07", "SW EQ07 Dynamic", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.16.0", "6-band dynamic EQ with spectral mode", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(eq07, Eq07)
