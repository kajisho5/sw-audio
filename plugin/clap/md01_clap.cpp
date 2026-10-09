// SW MD01 Chorus — CLAP plugin traits
#include "clap_adapter.hpp"
#include "md01/md01.hpp"

namespace {
struct Md01 {
    using Core = sw::md01::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::md01::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::md01::Mix;
    static constexpr int kUnitParam = sw::md01::Unit;   // Unit A / B / C
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_CHORUS, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.md01", "SW MD01 Chorus", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "BBD-style chorus", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(md01, Md01)
