// shared by the oversampling tests of the products (spec 共通機能「オーバーサンプリング」: 1x / 2x / 4x)
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>
namespace ost {
constexpr double kPi = 3.14159265358979323846, kFs = 48000.0;
// amplitude of the component at f in the second half of x (Hann window)
inline double bin(const std::vector<float>& x, double f, double fs = kFs) {
    const size_t n = x.size(), n0 = n / 2; std::complex<double> a; double w = 0;
    for (size_t i = n0; i < n; ++i) { const double h = 0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(i - n0) / static_cast<double>(n - n0 - 1)); a += h * static_cast<double>(x[i]) * std::exp(std::complex<double>(0, -2 * kPi * f * static_cast<double>(i) / fs)); w += h; }
    return 2 * std::abs(a) / w;
}
// what the processor makes of a sine at f0 (peak `amp`, both channels, blocks of 256 with the last one cut): the left channel
template <class P> std::vector<float> run(P& p, double f0, double amp, int n = 48000, int sidechain = 0) {
    (void)sidechain;
    std::vector<float> l(static_cast<size_t>(n)), r(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] = static_cast<float>(amp * std::sin(2 * kPi * f0 * i / kFs));
    for (int off = 0; off < n; off += 256) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, std::min(256, n - off)); }
    return l;
}
// the level at fx relative to the level at f0 (dB) of what the processor makes of a sine at f0: the aliases of a harmonic that is above the Nyquist frequency
template <class P> double relDb(P& p, double f0, double fx, double amp, int n = 48000) {
    const auto y = run(p, f0, amp, n);
    return 20 * std::log10((bin(y, fx) + 1e-12) / (bin(y, f0) + 1e-12));
}
// 4x is no worse than 2x (below -100 dB both are numerical noise)
inline bool notWorse(double a4, double a2) { return a4 < std::max(a2, -100.0) + 3.0; }
}  // namespace ost
