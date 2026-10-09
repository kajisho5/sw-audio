// SW VO08 Breath — CLAP plugin traits
#include "clap_adapter.hpp"
#include "vo08/vo08.hpp"

namespace {
struct Vo08 {
    using Core = sw::vo08::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::vo08::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 3;   // in a breath now (1 / 0), the gain the breath gets (dB), breaths counted since the start
    static void readouts(const Core& c, double* o) { o[0] = c.breathActive() ? 1.0 : 0.0; o[1] = c.gainDb(); o[2] = c.breathCount(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.vo08", "SW VO08 Breath", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Lower, remove or mark the breaths", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(vo08, Vo08)
