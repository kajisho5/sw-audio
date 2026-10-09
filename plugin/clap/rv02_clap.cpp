// SW RV02 Plate — CLAP plugin traits
#include "clap_adapter.hpp"
#include "rv02/rv02.hpp"

namespace {
struct Rv02 {
    using Core = sw::rv02::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::rv02::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::rv02::Mix;
    static constexpr int kUnitParam = sw::rv02::Unit;   // Unit A / B / C
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_REVERB, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.rv02", "SW RV02 Plate", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.16.0", "Plate reverb", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(rv02, Rv02)
