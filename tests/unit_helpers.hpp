// shared by the Unit A / B / C tests of the products (spec common function: component tolerances, a different set on the left and on the right channel)
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <utility>
#include <vector>
namespace unt {
constexpr double kPi = 3.14159265358979323846, kFs = 48000.0;
inline double binAmp(const std::vector<float>& x, double f) {
    const size_t n = x.size(), n0 = n / 2; std::complex<double> a; double w = 0;
    for (size_t i = n0; i < n; ++i) { const double h = 0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(i - n0) / static_cast<double>(n - n0 - 1)); a += h * static_cast<double>(x[i]) * std::exp(std::complex<double>(0, -2 * kPi * f * static_cast<double>(i) / kFs)); w += h; }
    return 2 * std::abs(a) / w;
}
// the same sine on both channels in; the level (dB re the input) of the left and of the right output at f
template <class P> std::pair<double, double> gainLR(P& p, double f, double amp = 0.01, int n = 48000) {
    std::vector<float> l(static_cast<size_t>(n)), r(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] = static_cast<float>(amp * std::sin(2 * kPi * f * i / kFs));
    for (int off = 0; off < n; off += 256) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, std::min(256, n - off)); }
    return {20 * std::log10(binAmp(l, f) / amp), 20 * std::log10(binAmp(r, f) / amp)};
}
// the largest left-right difference (dB) of the gain over some frequencies
template <class Make> double maxLRDiff(Make make, std::initializer_list<double> freqs, double amp = 0.01) {
    double m = 0; for (double f : freqs) { auto p = make(); const auto g = gainLR(p, f, amp); m = std::max(m, std::abs(g.first - g.second)); } return m;
}
}  // namespace unt
