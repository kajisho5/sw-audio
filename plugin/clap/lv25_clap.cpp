// SW LV25 Live Delay — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include <cstdlib>
#include <cstring>
#include "lv25/lv25.hpp"

namespace {
struct Lv25 {
    using Core = sw::lv25::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv25::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::lv25::Mix;
    static void guiCall(Core& c, const char* n, const char* a) { (void)a; if (!std::strcmp(n, "tap")) c.tap(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv25", "SW LV25 Live Delay", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Tap-tempo delay that rings out on bypass", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv25, Lv25)
