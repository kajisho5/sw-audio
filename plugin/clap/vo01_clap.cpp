// SW VO01 Tune — CLAP plugin traits
#include "clap_adapter.hpp"
#include "vo01/vo01.hpp"

namespace {
struct Vo01 {
    using Core = sw::vo01::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::vo01::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 3;   // voiced (1 / 0), the singer's pitch and the corrected pitch (MIDI note numbers)
    static void readouts(const Core& c, double* o) { o[0] = c.voiced() ? 1.0 : 0.0; o[1] = c.measuredSemitones(); o[2] = c.lastNoteSemitones(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_PITCH_CORRECTION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.vo01", "SW VO01 Tune", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Pitch correction", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(vo01, Vo01)
