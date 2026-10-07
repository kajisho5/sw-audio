// SW LV28 Remote Hub — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include <cstdlib>
#include <cstring>
#include "lv28/lv28.hpp"

namespace {
struct Lv28 {
    using Core = sw::lv28::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv28::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static void guiCall(Core& c, const char* n, const char* a) { (void)a; if (!std::strcmp(n, "lockall")) c.lockAll(a[0] == '1'); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv28", "SW LV28 Remote Hub", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Tablet remote control protected by a PIN", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv28, Lv28)
