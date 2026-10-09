// SW LV30 Recorder — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include <cstdlib>
#include <cstring>
#include "lv30/lv30.hpp"

namespace {
struct Lv30 {
    using Core = sw::lv30::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv30::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static constexpr bool kGuiCallOnGuiThread = true;   // file I/O: not on the audio thread
    static constexpr int kReadouts = 6;   // recording (1 / 0), seconds recorded, low disk (1 / 0), files written, marks made, the host's sample rate
    static void readouts(const Core& c, double* o) { o[0] = c.recording() ? 1.0 : 0.0; o[1] = c.secondsRecorded(); o[2] = c.lowDisk() ? 1.0 : 0.0; o[3] = c.filesWritten(); o[4] = c.marksMade(); o[5] = c.hostRate(); }
    static void guiCall(Core& c, const char* n, const char* a) {
        if (!std::strcmp(n, "record")) {
            if (a[0] == '1') {
                if (c.folder().empty()) { const char* h = std::getenv("HOME"); if (!h) h = std::getenv("USERPROFILE"); if (h) c.setFolder(std::string(h) + "/Documents/SW AUDIO"); }
                c.start();
            } else c.stop();
        } else if (!std::strcmp(n, "mark")) c.mark();
    }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv30", "SW LV30 Recorder", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Always-on backup recording with markers", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv30, Lv30)
