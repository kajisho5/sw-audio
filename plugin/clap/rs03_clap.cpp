// SW RS03 Dehum — CLAP plugin traits
#include "clap_adapter.hpp"
#include "rs03/rs03.hpp"

namespace {
struct Rs03 {
    using Core = sw::rs03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::rs03::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_RESTORATION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.rs03", "SW RS03 Dehum", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Mains hum and harmonics notch comb", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(rs03, Rs03)
