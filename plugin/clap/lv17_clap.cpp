// SW LV17 Bus Comp — CLAP plugin traits
#include "clap_adapter.hpp"
#include "lv17/lv17.hpp"

namespace {
struct Lv17 {
    using Core = sw::lv17::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv17::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 1;   // gain reduction (dB, <= 0)
    static void readouts(const Core& c, double* o) { o[0] = c.gainReductionDb(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_COMPRESSOR, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv17", "SW LV17 Bus Comp", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Live bus compressor", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv17, Lv17)
