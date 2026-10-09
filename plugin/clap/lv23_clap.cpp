// SW LV23 Loudness — CLAP plugin traits (LIVE line)
#include "clap_adapter.hpp"
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include "lv23/lv23.hpp"

namespace {
struct Lv23 {
    using Core = sw::lv23::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::lv23::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 10;  // as MT01, then dead air seen, true peak over
    static void readouts(const Core& c, double* o) { o[0] = c.momentary(); o[1] = c.shortTerm(); o[2] = c.integrated(); o[3] = c.range(); o[4] = c.truePeakDb(); o[5] = c.target(); o[6] = c.difference(); o[7] = c.inBand() ? 1.0 : 0.0; o[8] = c.deadAirSeen() ? 1.0 : 0.0; o[9] = c.tpOver() ? 1.0 : 0.0; }
    static constexpr bool kAutoGain = false;
    static constexpr bool kDelta = false;
    static void guiCall(Core& c, const char* n, const char* a) { (void)a; if (!std::strcmp(n, "reset")) c.reset(); }
    // Export log: the measurement log as a CSV in Documents/SW AUDIO (GUI thread: file output; the log is a fixed ring the audio thread fills, reading it here only risks one row of the newest second)
    static bool guiOnGui(const char* n) { return !std::strcmp(n, "export"); }
    static void guiCallGui(Core& c, const char*, const char*) {
        const std::string docs = sw::gui::documentsDir(); if (docs.empty()) return;
        namespace fs = std::filesystem; std::error_code ec; const fs::path dir = fs::u8path(docs) / "SW AUDIO"; fs::create_directories(dir, ec); if (ec) return;
        const std::time_t now = std::time(nullptr); std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &now);
#else
        localtime_r(&now, &tmv);
#endif
        char name[64]; std::strftime(name, sizeof name, "lv23-log-%Y%m%d-%H%M%S.csv", &tmv);
        c.setStartTime(static_cast<double>(now - c.logCount()));   // the clock column: the log began logCount seconds ago
        std::ofstream f(dir / name, std::ios::binary); if (f) f << c.exportCsv();
    }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_ANALYZER, CLAP_PLUGIN_FEATURE_MASTERING, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.lv23", "SW LV23 Loudness", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.16.0", "Broadcast loudness meter with log export", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(lv23, Lv23)
