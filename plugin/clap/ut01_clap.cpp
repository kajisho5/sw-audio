// SW UT01 Gain — CLAP plugin traits
#include "clap_adapter.hpp"
#include <cstdlib>
#include <cstring>
#include "ut01/ut01.hpp"

namespace {
struct Ut01 {
    using Core = sw::ut01::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::ut01::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static void guiCall(Core& c, const char* n, const char* a) { (void)a; if (!std::strcmp(n, "remember")) c.rememberGain(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.ut01", "SW UT01 Gain", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Gain, balance, width and polarity", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(ut01, Ut01)
