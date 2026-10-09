// SW CR03 Granular — CLAP plugin traits
#include "clap_adapter.hpp"
#include "cr03/cr03.hpp"

namespace {
struct Cr03 {
    using Core = sw::cr03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::cr03::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::cr03::Mix;
    // for the screen: grains playing, the chord (bit per pitch class), how many are listed, then per grain: how far back it reads (s), speed, place in its window, source span (s), pan
    static constexpr int kReadouts = 3 + Core::kScopeGrains * Core::kGrainValues;
    static void readouts(const Core& c, double* o) { o[0] = c.activeGrains(); o[1] = c.chordMask(); o[2] = c.grains(o + 3); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.cr03", "SW CR03 Granular", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Granular cloud, scatter and glitch", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(cr03, Cr03)
