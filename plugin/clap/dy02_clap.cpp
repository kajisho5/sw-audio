// SW DY02 Opto — CLAP plugin traits
#include "clap_adapter.hpp"
#include "dy02/dy02.hpp"

namespace {
struct Dy02 {
    using Core = sw::dy02::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::dy02::specs(); }
    static constexpr int kOutputParam = sw::dy02::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::dy02::Mix;
    static constexpr int kUnitParam = sw::dy02::Unit;   // Unit A / B / C
    static constexpr int kReadouts = 1;   // gain reduction of the compressor (dB, <= 0), the core's own value
    static void readouts(const Core& c, double* o) { o[0] = c.gainReductionDb(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_COMPRESSOR, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.dy02", "SW DY02 Opto", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Optical leveler with Ride", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(dy02, Dy02)
