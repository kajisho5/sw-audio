// SW CR02 Stutter — CLAP plugin traits
#include "clap_adapter.hpp"
#include <cstdlib>
#include <cstring>
#include "cr02/cr02.hpp"

namespace {
struct Cr02 {
    using Core = sw::cr02::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::cr02::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static void guiCall(Core& c, const char* n, const char* a) { (void)a; if (!std::strcmp(n, "randomize")) c.randomize(); else if (!std::strcmp(n, "clear")) c.clearPattern(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.cr02", "SW CR02 Stutter", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.16.0", "Beat-synchronised stutter", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(cr02, Cr02)
