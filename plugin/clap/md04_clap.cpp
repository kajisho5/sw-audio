// SW MD04 Tremolo Pan — CLAP plugin traits
#include "clap_adapter.hpp"
#include "md04/md04.hpp"

namespace {
struct Md04 {
    using Core = sw::md04::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::md04::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 1;   // LFO rate in use (Hz; from the note length with Sync)
    static void readouts(const Core& c, double* o) { o[0] = c.rateHz(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_TREMOLO, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.md04", "SW MD04 Tremolo Pan", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Tremolo, auto-pan and harmonic tremolo", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(md04, Md04)
