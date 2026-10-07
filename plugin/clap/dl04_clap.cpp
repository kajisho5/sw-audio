// SW DL04 Multitap — CLAP plugin traits
#include "clap_adapter.hpp"
#include "dl04/dl04.hpp"

namespace {
struct Dl04 {
    using Core = sw::dl04::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::dl04::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::dl04::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.dl04", "SW DL04 Multitap", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Six-tap delay", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(dl04, Dl04)
