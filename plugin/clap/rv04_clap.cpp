// SW RV04 Convolution — CLAP plugin traits
#include "clap_adapter.hpp"
#include "rv04/rv04.hpp"
#include <cstdio>
#include <cstring>

namespace {
struct Rv04 {
    using Core = sw::rv04::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::rv04::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::rv04::Mix;
    static constexpr int kReadouts = 3;   // IRs from the screen that worked / failed since the start, an IR is in place (1 / 0)
    static void readouts(const Core& c, double* o) { o[0] = c.irLoadsDone(); o[1] = c.irLoadsFailed(); o[2] = c.irLoaded() ? 1.0 : 0.0; }
    // the screen decodes an IR file and sends it as float samples in base64 pieces: irbegin <channels> <rate>, irdata <base64> ..., irend (or irabort). The screen's thread
    static constexpr bool kGuiCallOnGuiThread = true;
    static void guiCall(Core& c, const char* n, const char* a) {
        if (!std::strcmp(n, "irbegin")) { int ch = 0; double rate = 0.0; if (std::sscanf(a, "%d %lf", &ch, &rate) == 2) c.irBegin(ch, rate); else c.irAbort(); }
        else if (!std::strcmp(n, "irdata")) c.irAppendBase64(a);
        else if (!std::strcmp(n, "irend")) c.irCommit();
        else if (!std::strcmp(n, "irabort")) c.irAbort();
    }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_REVERB, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.rv04", "SW RV04 Convolution", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Convolution reverb", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(rv04, Rv04)
