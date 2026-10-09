// SW DL02 Tape Echo — CLAP plugin traits
#include "clap_adapter.hpp"
#include "dl02/dl02.hpp"

namespace {
struct Dl02 {
    using Core = sw::dl02::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::dl02::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::dl02::Mix;
    static constexpr int kUnitParam = sw::dl02::Unit;   // Unit A / B / C
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.dl02", "SW DL02 Tape Echo", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Three-head tape echo", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(dl02, Dl02)
