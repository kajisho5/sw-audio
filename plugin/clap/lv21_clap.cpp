// SW LV21 Test Gen — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include <cstdlib>
#include <cstring>
#include "lv21/lv21.hpp"

namespace {
struct Lv21 {
    using Core = sw::lv21::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv21::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static void guiCall(Core& c, const char* n, const char* a) { (void)a; if (!std::strcmp(n, "arm")) { if (a[0] == '1') c.arm(); else c.disarm(); } else if (!std::strcmp(n, "output")) { if (a[0] == '1') c.outputOn(); else c.outputOff(); } }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv21", "SW LV21 Test Gen", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Test signal generator, silent until armed", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv21, Lv21)
