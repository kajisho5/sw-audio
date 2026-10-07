// SW LV19 Av Sync — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include <cstdlib>
#include <cstring>
#include "lv19/lv19.hpp"

namespace {
struct Lv19 {
    using Core = sw::lv19::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv19::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static void guiCall(Core& c, const char* n, const char* a) { (void)a; if (!std::strcmp(n, "clap")) c.markVideoClap(a[0] ? std::atof(a) : 0.0); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv19", "SW LV19 Av Sync", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Audio / video delay correction with clap sync", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv19, Lv19)
