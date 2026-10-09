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
    static void guiCall(Core& c, const char* n, const char* a) { (void)a; if (!std::strcmp(n, "ringout")) c.ringOut(a[0] == '1'); else if (!std::strcmp(n, "lock")) c.lockFilters(); else if (!std::strcmp(n, "clearlive")) c.clearLive(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_FILTER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv02", "SW LV02 Feedback", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Howling suppressor with ring out", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv02, Lv02)
