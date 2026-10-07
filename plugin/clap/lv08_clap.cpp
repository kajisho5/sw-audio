// SW LV08 Room Noise — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include <cstdlib>
#include <cstring>
#include "lv08/lv08.hpp"

namespace {
struct Lv08 {
    using Core = sw::lv08::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv08::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static void guiCall(Core& c, const char* n, const char* a) { (void)a; if (!std::strcmp(n, "learn")) c.learnNoise(); else if (!std::strcmp(n, "forget")) c.clearLearned(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_RESTORATION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv08", "SW LV08 Room Noise", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Room noise, HVAC and keyboard suppressor", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv08, Lv08)
