// SW DY11 Multiband 6 — CLAP plugin traits
#include "clap_adapter.hpp"
#include "dy11/dy11.hpp"

namespace {
struct Dy11 {
    using Core = sw::dy11::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::dy11::specs(); }
    static constexpr int kOutputParam = -1;   // the core applies Output itself (routing it to the shell as well doubled the gain)
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_COMPRESSOR, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.dy11", "SW DY11 Multiband 6", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Six band dynamics", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(dy11, Dy11)
