// SW CR01 Filter — CLAP plugin traits
#include "clap_adapter.hpp"
#include "cr01/cr01.hpp"

namespace {
struct Cr01 {
    using Core = sw::cr01::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::cr01::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_FILTER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.cr01", "SW CR01 Filter", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.12.0", "Envelope, LFO or sidechain driven filter", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(cr01, Cr01)
