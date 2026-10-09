// SW EQ05 Console — CLAP plugin (traits for the generic adapter)
#include "clap_adapter.hpp"
#include "eq05/eq05.hpp"

namespace {
struct Eq05 {
    using Core = sw::eq05::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::eq05::specs(); }
    static constexpr int kOutputParam = sw::eq05::Output;
    static constexpr int kInParam = sw::eq05::In;
    static constexpr int kMixParam = -1;
    static constexpr int kUnitParam = sw::eq05::Unit;   // Unit A / B / C
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {
            CLAP_VERSION_INIT, "com.seventh-well.sw-audio.eq05", "SW EQ05 Console", "SEVENTHWELL",
            "https://seventh-well.com", "", "", "0.13.0", "Four band console EQ", features};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(eq05, Eq05)
