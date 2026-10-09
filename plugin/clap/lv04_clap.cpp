// SW LV04 Safety limiter — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include "lv04/lv04.hpp"

namespace {
struct Lv04 {
    using Core = sw::lv04::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv04::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 5;   // limit events in the log, events since the start, then of the last event: seconds since it started (-1: none yet), its deepest reduction (dB), its length (s)
    static void readouts(const Core& c, double* o) {
        o[0] = c.eventCount(); o[1] = static_cast<double>(c.totalEvents()); o[2] = -1.0; o[3] = 0.0; o[4] = 0.0;
        if (c.eventCount() > 0) { const auto e = c.event(c.eventCount() - 1); o[2] = static_cast<double>(c.nowSample() - e.startSample) / c.sampleRate(); o[3] = e.maxReductionDb; o[4] = static_cast<double>(e.lengthSamples) / c.sampleRate(); }
    }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_LIMITER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv04", "SW LV04 Safety limiter", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.15.0", "Live safety limiter with event log", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv04, Lv04)
