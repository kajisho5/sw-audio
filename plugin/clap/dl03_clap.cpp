// SW DL03 Bbd — CLAP plugin traits
#include "clap_adapter.hpp"
#include "dl03/dl03.hpp"

namespace {
struct Dl03 {
    using Core = sw::dl03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::dl03::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = sw::dl03::Mix;
    static constexpr int kUnitParam = sw::dl03::Unit;   // Unit A / B / C
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.dl03", "SW DL03 Bbd", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.0", "Bucket-brigade analog delay", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(dl03, Dl03)
