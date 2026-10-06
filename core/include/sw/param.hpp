// SW AUDIO core — parameter curves (spec: 共通章 3. カーブ定義)
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace sw {

enum class Curve { Lin, Log, Skew, Step, Fader };

struct ParamSpec {
    const char* id;
    const char* name;
    double min, max, def;
    Curve curve = Curve::Lin;
    double skew = 1.0;                 // k for Curve::Skew
    std::vector<double> steps = {};    // values for Curve::Step
    const char* unit = "";
    std::vector<std::string> labels = {};  // display labels for Curve::Step
    const char* minLabel = nullptr;         // e.g. "Off" shown at the minimum
    const char* maxLabel = nullptr;         // e.g. "Auto" or "inf" shown at the maximum
    double maxLabelNorm = 1.0;              // maxLabel applies from this normalized position (e.g. 0.95 = rightmost 5 %)
    bool automatable = true;
    bool reversed = false;                  // knob runs from max to min (e.g. Width: Narrow = Q 2.0 ... Wide = Q 0.4)                // false: monitoring controls such as Listen (spec: Auto —)

    int numSteps() const { return curve == Curve::Step ? static_cast<int>(steps.size()) : 0; }

    double toValue(double x) const {
        x = std::clamp(x, 0.0, 1.0);
        if (reversed && curve != Curve::Step) x = 1.0 - x;
        switch (curve) {
            case Curve::Lin:  return min + x * (max - min);
            case Curve::Log:  return min * std::pow(max / min, x);
            case Curve::Fader: {  // console fader law: Off | -100..-50 | -50..-20 | -20..0 | 0..+10 dB (0 dB at 75 %)
                if (x <= 0.0) return min;
                if (x < 0.25) return -100.0 + 200.0 * x;
                if (x < 0.5) return -50.0 + 120.0 * (x - 0.25);
                if (x < 0.75) return -20.0 + 80.0 * (x - 0.5);
                return std::min(max, 40.0 * (x - 0.75));
            }
            case Curve::Skew: return min + (max - min) * std::pow(x, skew);
            case Curve::Step: {
                if (steps.empty()) return min;
                const int n = numSteps();
                const int i = static_cast<int>(std::lround(x * (n - 1)));
                return steps[static_cast<size_t>(std::clamp(i, 0, n - 1))];
            }
        }
        return min;
    }

    double toNorm(double v) const {
        if (reversed && curve != Curve::Step) return 1.0 - baseNorm(v);
        return baseNorm(v);
    }
    double baseNorm(double v) const {
        switch (curve) {
            case Curve::Lin:  return std::clamp((v - min) / (max - min), 0.0, 1.0);
            case Curve::Log:  return std::clamp(std::log(v / min) / std::log(max / min), 0.0, 1.0);
            case Curve::Fader:
                if (v <= min) return 0.0;
                if (v < -50.0) return std::clamp((v + 100.0) / 200.0, 0.0, 0.25);
                if (v < -20.0) return 0.25 + (v + 50.0) / 120.0;
                if (v < 0.0) return 0.5 + (v + 20.0) / 80.0;
                return std::clamp(0.75 + v / 40.0, 0.0, 1.0);
            case Curve::Skew: return std::clamp(std::pow(std::clamp((v - min) / (max - min), 0.0, 1.0), 1.0 / skew), 0.0, 1.0);
            case Curve::Step: {
                const int n = numSteps();
                if (n < 2) return 0.0;
                int best = 0;
                for (int i = 1; i < n; ++i)
                    if (std::abs(steps[static_cast<size_t>(i)] - v) < std::abs(steps[static_cast<size_t>(best)] - v)) best = i;
                return static_cast<double>(best) / (n - 1);
            }
        }
        return 0.0;
    }
};

// ratio controls whose rightmost range means infinity (spec: 右端5 % は ∞)
inline bool isInfiniteRatio(const ParamSpec& s, double v) { return s.maxLabel != nullptr && s.toNorm(v) >= s.maxLabelNorm - 1e-12; }

}  // namespace sw
