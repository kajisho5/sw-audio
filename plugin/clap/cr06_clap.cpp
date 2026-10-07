// SW CR06 One Knob — CLAP plugin traits
#include "clap_adapter.hpp"
#include "cr06/cr06.hpp"

namespace {
struct Cr06 {
    using Core = sw::cr06::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::cr06::specs(); }
    static constexpr int kOutputParam = sw::cr06::Output;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::cr06::Mix;
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_FILTER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.cr06", "SW CR06 One Knob", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Six effects on one knob", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(cr06, Cr06)
