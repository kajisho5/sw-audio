// a sung-vowel-like test signal for the voice products: harmonics of f0 shaped by two formant resonances
#pragma once
#include "tu.hpp"
#include <cmath>
#include <functional>
#include <vector>

namespace tu {
// f0Of(t): the fundamental at time t (s)
inline std::vector<float> voiceFn(const std::function<double(double)>& f0Of, double seconds, double f1 = 700.0, double f2 = 1800.0, double amp = 0.3) {
    const size_t n = static_cast<size_t>(seconds * kFs); std::vector<float> y(n, 0.0f); double ph = 0;
    for (size_t i = 0; i < n; ++i) {
        const double f = f0Of(static_cast<double>(i) / kFs);
        ph += f / kFs; ph -= std::floor(ph);
        double s = 0; for (int h = 1; h * f < 6000; ++h) { const double fh = h * f; const double env = 1.0 / (1.0 + std::pow((fh - f1) / 150.0, 2.0)) + 0.6 / (1.0 + std::pow((fh - f2) / 250.0, 2.0)) + 0.02; s += env / h * std::sin(2 * 3.14159265358979323846 * h * ph); }
        y[i] = static_cast<float>(amp * s);
    }
    return y;
}
inline std::vector<float> voice(double f0, double seconds, double f0End = 0.0, double f1 = 700.0, double f2 = 1800.0, double amp = 0.3) {
    return voiceFn([&](double t) { return f0End > 0.0 ? f0 + (f0End - f0) * t / seconds : f0; }, seconds, f1, f2, amp);
}
// vibrato: +-semis at hz around f0
inline std::vector<float> vibratoVoice(double f0, double seconds, double semis, double hz) {
    return voiceFn([&](double t) { return f0 * std::exp2(semis * std::sin(2 * 3.14159265358979323846 * hz * t) / 12.0); }, seconds);
}
// the fundamental near `guess` in a window (a fine scan of the Hann spectrum)
inline double peakHz(const std::vector<float>& y, double lo, double hi, size_t a, size_t b) { double best = lo, bv = -1e9; for (double f = lo; f <= hi; f += 0.5) { const double v = binDb(y, f, a, b); if (v > bv) { bv = v; best = f; } } return best; }
inline double semis(double f, double ref = 440.0) { return 69.0 + 12.0 * std::log2(f / ref); }
}
