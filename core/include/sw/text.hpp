// SW AUDIO core — value <-> text (spec 共通章 2「数値表示」)
// dB: 0.1 step; Hz: <1 kHz integer Hz, 1-10 kHz one decimal kHz, >=10 kHz integer kHz; Log without unit (Q): 2 decimals.
// Formatting always rounds first, so text -> value -> text is stable.
#pragma once
#include "sw/param.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace sw {

inline double roundTo(double v, double step) {
    const double r = std::round(v / step) * step;
    return r == 0.0 ? 0.0 : r;  // never print a negative zero
}

inline bool isDbUnit(const std::string& u) { return u == "dB" || u == "dBFS" || u == "dBTP" || u == "LUFS" || u == "LU"; }

// numeric part only (no end labels); always rounds first so the text is stable
inline std::string formatNumber(const ParamSpec& s, double v) {
    char b[64];
    const std::string unit = s.unit;
    if (isDbUnit(unit)) { std::snprintf(b, sizeof b, "%.1f %s", roundTo(v, 0.1), unit.c_str()); return b; }
    if (unit == "Hz") {
        const double hz = roundTo(v, 1.0);
        if (hz < 1000) { std::snprintf(b, sizeof b, "%.0f Hz", hz); return b; }
        const double k1 = roundTo(v / 1000.0, 0.1);
        if (k1 < 10) std::snprintf(b, sizeof b, "%.1f kHz", k1);
        else std::snprintf(b, sizeof b, "%.0f kHz", roundTo(v / 1000.0, 1.0));
        return b;
    }
    if (unit == "ms") {
        const double r1 = roundTo(v, 0.1);
        if (r1 < 10) std::snprintf(b, sizeof b, "%.1f ms", r1);
        else std::snprintf(b, sizeof b, "%.0f ms", roundTo(v, 1.0));
        return b;
    }
    if (unit == "s") { std::snprintf(b, sizeof b, "%.2f s", roundTo(v, 0.01)); return b; }
    if (unit == "%") { std::snprintf(b, sizeof b, "%.0f %%", roundTo(v, 1.0)); return b; }
    if (unit == "Q") { std::snprintf(b, sizeof b, "Q %.2f", roundTo(v, 0.01)); return b; }
    if (unit == ":1") { std::snprintf(b, sizeof b, "%.1f:1", roundTo(v, 0.1)); return b; }
    if (s.curve == Curve::Log) std::snprintf(b, sizeof b, "%.2f", roundTo(v, 0.01));
    else std::snprintf(b, sizeof b, "%.1f", roundTo(v, 0.1));
    return b;
}

inline bool parseNumber(const std::string& text, double& out) {
    const char* c = text.c_str();
    if (c[0] == 'Q' && c[1] == ' ') c += 2;  // "Q 0.80"
    char* end = nullptr;
    double v = std::strtod(c, &end);
    if (end == c || !std::isfinite(v)) return false;
    while (*end == ' ') ++end;
    if (std::strncmp(end, "kHz", 3) == 0) v *= 1000.0;
    out = v;
    return true;
}

inline std::string formatValue(const ParamSpec& s, double v) {
    if (s.curve == Curve::Step && !s.labels.empty()) {
        const int i = static_cast<int>(std::lround(s.toNorm(v) * (s.numSteps() - 1)));
        return s.labels[static_cast<size_t>(i)];
    }
    const std::string t = formatNumber(s, v);
    double shown = v;
    parseNumber(t, shown);  // the label decision uses the displayed (rounded) value -> stable round trip
    if (s.minLabel && shown <= s.min) return s.minLabel;
    if (s.maxLabel && (s.maxLabelNorm < 1.0 ? s.toNorm(shown) >= s.maxLabelNorm - 1e-12 : shown >= s.max)) return s.maxLabel;
    return t;
}

inline bool parseValue(const ParamSpec& s, const std::string& text, double& out) {
    if (s.curve == Curve::Step && !s.labels.empty()) {
        for (size_t i = 0; i < s.labels.size(); ++i)
            if (text == s.labels[i]) { out = s.steps[i]; return true; }
        return false;
    }
    if (s.minLabel && text == s.minLabel) { out = s.min; return true; }
    if (s.maxLabel && text == s.maxLabel) { out = s.max; return true; }
    return parseNumber(text, out);
}

}  // namespace sw
