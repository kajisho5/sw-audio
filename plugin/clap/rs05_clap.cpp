// SW RS05 Declip — CLAP plugin traits
#include "clap_adapter.hpp"
#include "rs05/rs05.hpp"
#include <algorithm>
#include <cmath>

namespace {
struct Rs05 {
    using Core = sw::rs05::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::rs05::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    // for the screen: runs restored, the ceiling in use (dBFS), the length of the waveform window (ms), then what came in and what goes out (channel 0, 64 points each)
    static constexpr int kReadouts = 3 + 2 * Core::kScopeBins;
    static void readouts(const Core& c, double* o) {
        o[0] = c.runsRestored(); o[1] = 20.0 * std::log10(std::max(c.ceilingUsed(), 1e-6)); o[2] = c.scopeMs();
        c.scope(o + 3, o + 3 + Core::kScopeBins);
    }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_RESTORATION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.rs05", "SW RS05 Declip", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Restores clipped peaks", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(rs05, Rs05)
