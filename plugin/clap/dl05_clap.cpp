// SW DL05 Reverse — CLAP plugin traits
#include "clap_adapter.hpp"
#include "dl05/dl05.hpp"

namespace {
struct Dl05 {
    using Core = sw::dl05::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::dl05::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::dl05::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.dl05", "SW DL05 Reverse", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Reverse, forward and random grain delay", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(dl05, Dl05)
