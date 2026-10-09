// SW MD03 Phaser — CLAP plugin traits
#include "clap_adapter.hpp"
#include "md03/md03.hpp"

namespace {
struct Md03 {
    using Core = sw::md03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::md03::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::md03::Mix;
    static constexpr int kUnitParam = sw::md03::Unit;   // Unit A / B / C
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_PHASER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.md03", "SW MD03 Phaser", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.16.0", "Swept all-pass phaser", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(md03, Md03)
