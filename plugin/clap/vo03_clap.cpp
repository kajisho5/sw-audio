// SW VO03 Harmony — CLAP plugin traits
#include "clap_adapter.hpp"
#include "vo03/vo03.hpp"

namespace {
struct Vo03 {
    using Core = sw::vo03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::vo03::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_PITCH_SHIFTER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.vo03", "SW VO03 Harmony", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.1", "Harmony voices from one voice", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(vo03, Vo03)
