// The zoom of the plug-in window (the design's "100%" chip in the header): which sizes it steps through, the size the host is asked for, the size a dragged window edge snaps to, and the remembered choice
// (<Documents>/SW AUDIO/zoom.txt: one number in percent, shared by every product, so a person who likes 150 % has it everywhere).
// The page draws the design at the size of the view (ui/sw-ui.js, "fit"); the plug-in only decides how big the view is.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace sw::gui::zoom {

constexpr double kMin = 0.5, kMax = 2.0;
constexpr int kSteps[] = {75, 100, 125, 150, 200};

// NaN reads as "no zoom" (100 %); anything else is held within 50 % .. 200 %
inline double clamp(double z) {
    if (!(z == z)) return 1.0;
    return std::min(kMax, std::max(kMin, z));
}

// the chip's next size in percent: the first step above the current one, and the first step again after the last (a window dragged to 110 % goes to 125 %)
inline int nextPercent(int current) {
    for (int s : kSteps) if (s > current) return s;
    return kSteps[0];
}

// the size of the window for a factor; base = the design's size in the unit of the host's window API (times the host's scale where the host counts in pixels)
inline void sizeFor(double z, double baseW, double baseH, uint32_t& w, uint32_t& h) {
    z = clamp(z);
    w = static_cast<uint32_t>(std::lround(baseW * z));
    h = static_cast<uint32_t>(std::lround(baseH * z));
}

// the factor of a size the host proposes (a window edge dragged): the design stays whole, so the smaller of the two ratios decides
inline double fromSize(uint32_t w, uint32_t h, double baseW, double baseH) {
    if (!(baseW > 0.0) || !(baseH > 0.0)) return 1.0;
    return clamp(std::min(static_cast<double>(w) / baseW, static_cast<double>(h) / baseH));
}

inline std::filesystem::path file(const std::string& documents) { return std::filesystem::u8path(documents) / "SW AUDIO" / "zoom.txt"; }

// the remembered zoom in percent (50 .. 200); 100 when nothing sensible is there
inline int load(const std::string& documents) {
    if (documents.empty()) return 100;
    std::ifstream f(file(documents), std::ios::binary); if (!f) return 100;
    char buf[32] = {}; f.read(buf, sizeof buf - 1);
    char* end = nullptr; const long v = std::strtol(buf, &end, 10);
    if (end == buf) return 100;
    return static_cast<int>(std::lround(clamp(static_cast<double>(v) / 100.0) * 100.0));
}

inline bool save(const std::string& documents, int percent) {
    if (documents.empty()) return false;
    std::error_code ec; std::filesystem::create_directories(file(documents).parent_path(), ec); if (ec) return false;
    std::ofstream f(file(documents), std::ios::binary | std::ios::trunc); if (!f) return false;
    f << static_cast<int>(std::lround(clamp(percent / 100.0) * 100.0)) << "\n";
    return static_cast<bool>(f);
}

}  // namespace sw::gui::zoom
