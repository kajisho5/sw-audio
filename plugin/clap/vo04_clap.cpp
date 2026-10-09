// SW VO04 Doubler — CLAP plugin traits
#include "clap_adapter.hpp"
#include "vo04/vo04.hpp"

namespace {
struct Vo04 {
    using Core = sw::vo04::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::vo04::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::vo04::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_CHORUS, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.vo04", "SW VO04 Doubler", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Natural doubles with wandering timing", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(vo04, Vo04)
