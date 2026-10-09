// SW DY07 Snap — CLAP plugin traits
#include "clap_adapter.hpp"
#include "dy07/dy07.hpp"

namespace {
struct Dy07 {
    using Core = sw::dy07::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::dy07::specs(); }
    static constexpr int kOutputParam = sw::dy07::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::dy07::Mix;
    static constexpr int kUnitParam = sw::dy07::Unit;   // Unit A / B / C
    static constexpr int kReadouts = 1;   // gain reduction of the compressor (dB, <= 0), the core's own value
    static void readouts(const Core& c, double* o) { o[0] = c.gainReductionDb(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_COMPRESSOR, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.dy07", "SW DY07 Snap", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.16.0", "VCA compressor with attack shaper", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(dy07, Dy07)
