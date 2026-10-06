// SW MS05 Leveler — CLAP plugin traits
#include "clap_adapter.hpp"
#include "ms05/ms05.hpp"

namespace {
struct Ms05 {
    using Core = sw::ms05::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::ms05::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.ms05", "SW MS05 Leveler", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.12.0", "Leveler that writes its rides as automation", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(ms05, Ms05)
