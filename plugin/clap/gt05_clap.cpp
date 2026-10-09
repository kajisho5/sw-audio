// SW GT05 Reamp — CLAP plugin traits
#include "clap_adapter.hpp"
#include "gt05/gt05.hpp"

namespace {
struct Gt05 {
    using Core = sw::gt05::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::gt05::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kUnitParam = sw::gt05::Unit;   // Unit A / B / C
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_FILTER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.gt05", "SW GT05 Reamp", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.16.0", "Reamp interface: pickup, cable and load", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(gt05, Gt05)
