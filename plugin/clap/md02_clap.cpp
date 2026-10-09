// SW MD02 Flanger — CLAP plugin traits
#include "clap_adapter.hpp"
#include "md02/md02.hpp"

namespace {
struct Md02 {
    using Core = sw::md02::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::md02::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::md02::Mix;
    static constexpr int kReadouts = 2;   // sweep rate in use (Hz; from the note length with Sync), LFO phase (0 .. 1)
    static void readouts(const Core& c, double* o) { o[0] = c.rateHz(); o[1] = c.lfoPhase(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_FLANGER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.md02", "SW MD02 Flanger", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Flanger with through-zero", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(md02, Md02)
