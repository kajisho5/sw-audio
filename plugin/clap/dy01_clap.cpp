// SW DY01 FET — CLAP plugin traits
#include "clap_adapter.hpp"
#include "dy01/dy01.hpp"

namespace {
struct Dy01 {
    using Core = sw::dy01::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::dy01::specs(); }
    static constexpr int kOutputParam = sw::dy01::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::dy01::Mix;
    static constexpr int kReadouts = 1;   // gain reduction of the compressor (dB, <= 0), the core's own value
    static void readouts(const Core& c, double* o) { o[0] = c.gainReductionDb(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_COMPRESSOR, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.dy01", "SW DY01 FET", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "FET character compressor", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(dy01, Dy01)
