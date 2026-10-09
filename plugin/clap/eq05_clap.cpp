// SW EQ05 Console — CLAP plugin (traits for the generic adapter)
#include "clap_adapter.hpp"
#include "eq05/eq05.hpp"
#include <cstdlib>
#include <cstring>

namespace {
struct Eq05 {
    using Core = sw::eq05::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::eq05::specs(); }
    static constexpr int kOutputParam = sw::eq05::Output;
    static constexpr int kInParam = sw::eq05::In;
    static constexpr int kMixParam = -1;
    static constexpr int kUnitParam = sw::eq05::Unit;   // Unit A / B / C
    // Match: the screen's "Match" is a call on the audio thread (it listens to the input); the reference arrives and the fit runs on the screen's thread
    //   refbegin <rate>, refdata <base64>, refend (or refabort), refclear, and fit (when the readouts say the input has been heard)
    static constexpr int kReadouts = 9;   // listening (1 / 0), how far the 10 s of playing are (0 .. 1), the input has been heard and waits for fit (1 / 0), a reference is in place (1 / 0), the tone curves' difference (RMS dB) before / after the fit,
                                          // fits applied, reference loads that worked / failed since the start
    static void readouts(const Core& c, double* o) {
        o[0] = c.matching() ? 1.0 : 0.0; o[1] = c.matchProgress(); o[2] = c.needsFit() ? 1.0 : 0.0; o[3] = c.hasReference() ? 1.0 : 0.0; o[4] = c.matchBefore(); o[5] = c.matchAfter();
        o[6] = c.matchesApplied(); o[7] = c.refLoadsDone(); o[8] = c.refLoadsFailed();
    }
    // SW Link: the reference can be UT03's (its long-term spectrum), taken when the page sends "linkref"
    static constexpr const char* kLinkRefFrom = "UT03";
    static void linkRefUse(Core& c, const double* db) { c.refFromBands(db); }
    static void guiCall(Core& c, const char* n, const char*) { if (!std::strcmp(n, "match")) c.match(); }
    static bool guiOnGui(const char* n) { return !std::strncmp(n, "ref", 3) || !std::strcmp(n, "fit"); }
    static void guiCallGui(Core& c, const char* n, const char* a) {
        if (!std::strcmp(n, "refbegin")) c.refBegin(std::atof(a));
        else if (!std::strcmp(n, "refdata")) c.refAppendBase64(a);
        else if (!std::strcmp(n, "refend")) c.refCommit();
        else if (!std::strcmp(n, "refabort")) c.refAbort();
        else if (!std::strcmp(n, "refclear")) c.refClear();
        else if (!std::strcmp(n, "fit")) c.fit();
    }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {
            CLAP_VERSION_INIT, "com.seventh-well.sw-audio.eq05", "SW EQ05 Console", "SEVENTHWELL",
            "https://seventh-well.com", "", "", "0.16.0", "Four band console EQ", features};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(eq05, Eq05)
