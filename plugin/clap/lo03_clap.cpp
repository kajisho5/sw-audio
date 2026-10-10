// SW LO03 Low Focus — CLAP plugin traits
#include "clap_adapter.hpp"
#include "lo03/lo03.hpp"

namespace {
struct Lo03 {
    using Core = sw::lo03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lo03::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 2;   // the Focus bell's gain right now (dB, <= 0); 1 while the key is the Kick of another LO03 (SW Link), else 0 (the adapter writes it)
    static void readouts(const Core& c, double* o) { o[0] = c.focusCutDb(); o[1] = 0.0; }
    // SW Link: a Bass takes the Kick of another LO03 in this host as its key (the Role of every instance is its tag: 1 Kick, 2 Bass, 3 Both) - unless a sidechain with a signal is connected
    static const char* linkKeyOf(const Core& c) { return c.role() == sw::lo03::Bass ? "LO03" : nullptr; }
    static constexpr int kLinkKeyReadout = 1;
    static constexpr uint32_t kLinkKeyTag = 1;
    static constexpr bool kLinkKeyOnlyWithoutSidechain = true;
    static uint32_t linkTagOf(const Core& c) { return static_cast<uint32_t>(c.role()) + 1u; }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_FILTER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lo03", "SW LO03 Low Focus", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.16.0", "Kick and bass low-end separation", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lo03, Lo03)
