// SW MD06 Freq Shift — CLAP plugin traits
#include "clap_adapter.hpp"
#include "md06/md06.hpp"

namespace {
struct Md06 {
    using Core = sw::md06::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::md06::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::md06::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_FREQUENCY_SHIFTER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.md06", "SW MD06 Freq Shift", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.1", "Frequency shifter", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(md06, Md06)
