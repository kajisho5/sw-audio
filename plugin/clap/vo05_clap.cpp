// SW VO05 Rider — CLAP plugin traits
#include "clap_adapter.hpp"
#include "vo05/vo05.hpp"

namespace {
struct Vo05 {
    using Core = sw::vo05::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::vo05::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.vo05", "SW VO05 Rider", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Rides the vocal against the music", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(vo05, Vo05)
