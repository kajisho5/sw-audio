// SW LV23 Loudness — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include <cstdlib>
#include <cstring>
#include "lv23/lv23.hpp"

namespace {
struct Lv23 {
    using Core = sw::lv23::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv23::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 10;  // as MT01, then dead air seen, true peak over
    static void readouts(const Core& c, double* o) { o[0] = c.momentary(); o[1] = c.shortTerm(); o[2] = c.integrated(); o[3] = c.range(); o[4] = c.truePeakDb(); o[5] = c.target(); o[6] = c.difference(); o[7] = c.inBand() ? 1.0 : 0.0; o[8] = c.deadAirSeen() ? 1.0 : 0.0; o[9] = c.tpOver() ? 1.0 : 0.0; }
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static void guiCall(Core& c, const char* n, const char* a) { (void)a; if (!std::strcmp(n, "reset")) c.reset(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_ANALYZER, CLAP_PLUGIN_FEATURE_MASTERING, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv23", "SW LV23 Loudness", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Broadcast loudness meter with log export", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv23, Lv23)
