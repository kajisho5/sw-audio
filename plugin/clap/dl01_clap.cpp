// SW DL01 Echo — CLAP plugin traits
#include "clap_adapter.hpp"
#include "dl01/dl01.hpp"

namespace {
struct Dl01 {
    using Core = sw::dl01::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::dl01::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::dl01::Mix;
    static constexpr int kUnitParam = sw::dl01::Unit;   // Unit A / B / C
    static constexpr int kReadouts = 2;   // the delay in use (s; a note length when Sync is on and the host has a tempo), the host tempo (bpm, 0 = none)
    static void readouts(const Core& c, double* o) { o[0] = c.timeSeconds(); o[1] = c.tempo(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.dl01", "SW DL01 Echo", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Echo with tape, analog and digital character", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(dl01, Dl01)
