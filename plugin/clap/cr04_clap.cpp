// SW CR04 Freeze — CLAP plugin traits
#include "clap_adapter.hpp"
#include "cr04/cr04.hpp"

namespace {
struct Cr04 {
    using Core = sw::cr04::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::cr04::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.cr04", "SW CR04 Freeze", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Spectral freeze", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(cr04, Cr04)
