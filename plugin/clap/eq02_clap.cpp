// SW EQ02 Surgical — CLAP plugin traits
#include "clap_adapter.hpp"
#include "eq02/eq02.hpp"
#include <cstring>

namespace {
struct Eq02 {
    using Core = sw::eq02::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::eq02::specs(); }
    static constexpr int kOutputParam = sw::eq02::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    // Assist: on (1 / 0), then the resonances of the input, 6 x [Hz, dB it sticks out] (0 when there are fewer)
    static constexpr int kReadouts = 1 + 2 * sw::ResonanceFinder::kMarks;
    static void readouts(const Core& c, double* o) { o[0] = c.assist() ? 1.0 : 0.0; sw::ResonanceFinder::Mark m[sw::ResonanceFinder::kMarks]; const int n = c.resonances(m); for (int i = 0; i < n; ++i) { o[1 + 2 * i] = m[i].hz; o[2 + 2 * i] = m[i].db; } }
    static void guiCall(Core& c, const char* n, const char* a) { if (!std::strcmp(n, "assist")) c.setAssist(a[0] == '1'); }   // the screen's Assist button (on the audio thread)
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.eq02", "SW EQ02 Surgical", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "24-band surgical and dynamic EQ", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(eq02, Eq02)
