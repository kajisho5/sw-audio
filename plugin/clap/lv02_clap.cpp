// SW LV02 Feedback — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include <cstdlib>
#include <cstring>
#include "lv02/lv02.hpp"

namespace {
struct Lv02 {
    using Core = sw::lv02::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv02::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 25;   // per filter slot F1..F12: frequency (Hz, 0 = unused) and depth (dB; +100 = FIXED); then ring out (1 / 0)
    static void readouts(const Core& c, double* o) {
        const auto& g = c.guard();
        for (int i = 0; i < 12; ++i) {
            const auto& s = g.slot(i);
            const bool on = i < g.slots() && s.used && !s.freeing && s.targetDb > 0.0;
            o[2 * i] = on ? s.freq : 0.0; o[2 * i + 1] = on ? s.depthDb + (s.fixed ? 100.0 : 0.0) : 0.0;
        }
        o[24] = g.ringingOut() ? 1.0 : 0.0;
    }
    static void guiCall(Core& c, const char* n, const char* a) { (void)a; if (!std::strcmp(n, "ringout")) c.ringOut(a[0] == '1'); else if (!std::strcmp(n, "lock")) c.lockFilters(); else if (!std::strcmp(n, "clearlive")) c.clearLive(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_FILTER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv02", "SW LV02 Feedback", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Howling suppressor with ring out", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv02, Lv02)
