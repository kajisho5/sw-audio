// SW DY09 Transient — CLAP plugin traits
#include "clap_adapter.hpp"
#include "dy09/dy09.hpp"

namespace {
struct Dy09 {
    using Core = sw::dy09::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::dy09::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::dy09::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_COMPRESSOR, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.dy09", "SW DY09 Transient", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.12.0", "Transient shaper", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(dy09, Dy09)
