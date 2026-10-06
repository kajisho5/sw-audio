// shared measurement helpers for EQ tests
#pragma once
#include <cmath>
#include <complex>
#include <vector>
namespace eqt {
constexpr double kPi = 3.14159265358979323846, kFs = 48000.0;
// steady-state level of a sine (DFT at f over the second half), relative to the input amplitude
template <class P> double gainDb(P& p, double f, double amp = 0.001, int side = 0) {
    const int n = 48000; std::vector<float> l(n), r(n);
    for (int i = 0; i < n; ++i) { const float v = static_cast<float>(amp * std::sin(2 * kPi * f * i / kFs)); l[i] = v; r[i] = side ? -v : v; }
    for (int off = 0; off < n; off += 256) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, std::min(256, n - off)); }
    std::complex<double> acc; double w = 0;
    for (int i = n / 2; i < n; ++i) { const double h = 0.5 - 0.5 * std::cos(2 * kPi * (i - n / 2) / (n / 2 - 1)); acc += h * static_cast<double>(l[i]) * std::exp(std::complex<double>(0, -2 * kPi * f * i / kFs)); w += h; }
    return 20 * std::log10(2 * std::abs(acc) / w / amp);
}
// level of harmonic k of a sine at f, relative to the fundamental (dB)
template <class P> double harmonicDb(P& p, double f, int k, double amp) {
    const int n = 48000; std::vector<float> l(n), r(n);
    for (int i = 0; i < n; ++i) l[i] = r[i] = static_cast<float>(amp * std::sin(2 * kPi * f * i / kFs));
    for (int off = 0; off < n; off += 256) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, std::min(256, n - off)); }
    auto bin = [&](double fr) { std::complex<double> a; for (int i = n / 2; i < n; ++i) { const double h = 0.5 - 0.5 * std::cos(2 * kPi * (i - n / 2) / (n / 2 - 1)); a += h * static_cast<double>(l[i]) * std::exp(std::complex<double>(0, -2 * kPi * fr * i / kFs)); } return std::abs(a); };
    return 20 * std::log10(bin(f * k) / bin(f) + 1e-12);
}
}
