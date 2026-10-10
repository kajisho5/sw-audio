// SW DY04 Gate — CLAP plugin traits
#include "clap_adapter.hpp"
#include "dy04/dy04.hpp"

namespace {
struct Dy04 {
    using Core = sw::dy04::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::dy04::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_GATE, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.dy04", "SW DY04 Gate", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Gate, expander and ducker", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(dy04, Dy04)
