// SW CR05 Tape Stop — CLAP plugin traits
#include "clap_adapter.hpp"
#include "cr05/cr05.hpp"

namespace {
struct Cr05 {
    using Core = sw::cr05::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::cr05::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.cr05", "SW CR05 Tape Stop", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.12.0", "Tape stop, start and spin back", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(cr05, Cr05)
