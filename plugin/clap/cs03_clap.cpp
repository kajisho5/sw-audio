// SW CS03 Stepped Strip — CLAP plugin traits
#include "clap_adapter.hpp"
#include "cs03/cs03.hpp"
#include <cstring>

namespace {
struct Cs03 {
    using Core = sw::cs03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::cs03::specs(); }
    static constexpr int kOutputParam = sw::cs03::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kUnitParam = sw::cs03::Unit;   // Unit A / B / C
    static constexpr int kReadouts = 2;   // input level: listening (1 / 0), how far the 5 s are (0 .. 1)
    static void readouts(const Core& c, double* o) { o[0] = c.learning() ? 1.0 : 0.0; o[1] = c.learnProgress(); }
    static void guiCall(Core& c, const char* n, const char*) { if (!std::strcmp(n, "learn")) c.learn(); }   // the screen's "Set input" (on the audio thread)
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_COMPRESSOR, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.cs03", "SW CS03 Stepped Strip", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Stepped channel strip", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(cs03, Cs03)
