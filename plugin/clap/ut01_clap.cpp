// SW UT01 Gain — CLAP plugin traits
#include "clap_adapter.hpp"
#include <cstdlib>
#include <cstring>
#include "ut01/ut01.hpp"

namespace {
struct Ut01 {
    using Core = sw::ut01::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::ut01::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static void guiCall(Core& c, const char* n, const char* a) { (void)a; if (!std::strcmp(n, "remember")) c.rememberGain(); }
    // the host's track: its name picks the kind (Vocal, Drums ...); a bus or the master is a Bus whatever its name says
    static void trackInfo(Core& c, const char* name, uint64_t flags) {
        if (flags & (CLAP_TRACK_INFO_IS_FOR_BUS | CLAP_TRACK_INFO_IS_FOR_MASTER)) c.setTrackKind(sw::ut01::Bus);
        else if (name && name[0]) c.setTrackName(name);
    }
    // read-outs for the screen: the kind of track (-1: the host has not said; the Gain is then kept for Other), whether a Gain is remembered for the kind, that Gain in dB
    static constexpr int kReadouts = 3;
    static void readouts(const Core& c, double* o) { double db = 0; const bool has = c.suggestedGainDb(db); o[0] = c.trackKnown() ? c.trackKind() : -1; o[1] = has ? 1.0 : 0.0; o[2] = has ? db : 0.0; }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.ut01", "SW UT01 Gain", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.16.0", "Gain, balance, width and polarity", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(ut01, Ut01)
