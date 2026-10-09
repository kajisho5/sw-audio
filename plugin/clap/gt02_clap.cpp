// SW GT02 Cab IR — CLAP plugin traits
#include "clap_adapter.hpp"
#include "gt02/gt02.hpp"

namespace {
struct Gt02 {
    using Core = sw::gt02::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::gt02::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_FILTER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.gt02", "SW GT02 Cab IR", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.16.0", "Guitar cabinet and microphone", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(gt02, Gt02)
