// SW UT03 Reference — CLAP plugin traits
#include "clap_adapter.hpp"
#include "ut03/ut03.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
struct Ut03 {
    using Core = sw::ut03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::ut03::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    // match (dB), input integrated LUFS, reference 1 / 2 LUFS, then per reference: length (s, 0 = none loaded), loop region start / end (s); loads that worked / failed since the start
    static constexpr int kReadouts = 12;
    static void readouts(const Core& c, double* o) {
        o[0] = c.matchDb(); o[1] = c.inputLufs(); o[2] = c.referenceLufs(1); o[3] = c.referenceLufs(2);
        for (int s = 1; s <= 2; ++s) { double a, b; c.regionOf(s, a, b); o[4 + 3 * (s - 1)] = c.referenceSeconds(s); o[5 + 3 * (s - 1)] = a; o[6 + 3 * (s - 1)] = b; }
        o[10] = c.loadsDone(); o[11] = c.loadsFailed();
    }
    // the screen sends a reference file in pieces (it cannot hand over a path): refbegin <slot>, refdata <base64> ..., refend (decode and load); refclear <slot>. The screen's thread: decoding takes a moment
    static constexpr bool kGuiCallOnGuiThread = true;
    static void guiCall(Core& c, const char* n, const char* a) {
        if (!std::strcmp(n, "refbegin")) c.stageBegin(std::atoi(a));
        else if (!std::strcmp(n, "refdata")) c.stageAppendBase64(a);
        else if (!std::strcmp(n, "refend")) c.stageCommit();
        else if (!std::strcmp(n, "refabort")) c.stageAbort();
        else if (!std::strcmp(n, "refclear")) c.clearReference(std::atoi(a));
        else if (!std::strcmp(n, "looprange")) { double s = 0.0, e = 0.0; if (std::sscanf(a, "%lf %lf", &s, &e) == 2) c.setLoopRegion(s, e); }   // the Custom loop region, dragged on the screen (seconds)
    }
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_ANALYZER, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.ut03", "SW UT03 Reference", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "A/B against reference tracks at matched loudness", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(ut03, Ut03)
