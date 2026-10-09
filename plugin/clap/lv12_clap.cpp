// SW LV12 Geq 31 — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include <cstdlib>
#include <cstring>
#include "lv12/lv12.hpp"

namespace {
struct Lv12 {
    using Core = sw::lv12::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv12::specs(); }
    static constexpr int kOutputParam = sw::lv12::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static void guiCall(Core& c, const char* n, const char* a) { (void)a; if (!std::strcmp(n, "flat")) c.flat(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv12", "SW LV12 Geq 31", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.16.0", "31-band graphic EQ with feedback guard", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv12, Lv12)
