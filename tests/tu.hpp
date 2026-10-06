// shared helpers for product tests (namespace tu): portable noise, tones, block runner, level / spectrum measurements
#pragma once
#include "near.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <random>
#include <vector>
namespace tu {
constexpr double kPi = 3.14159265358979323846, kFs = 48000.0;
// portable Gaussian noise (std::normal_distribution differs between standard libraries)
struct Gauss {
    std::mt19937 rng; explicit Gauss(unsigned seed = 1) : rng(seed) {}
    double uni() { return (rng() + 0.5) / 4294967296.0; }
    double gauss() { return std::sqrt(-2 * std::log(uni())) * std::cos(2 * kPi * uni()); }
};
// stereo (identical L/R) blocks of at most 256 samples through p.process; returns the left channel
template <class P> std::vector<float> run(P& p, std::vector<float> l) {
    std::vector<float> r = l;
    for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); }
    return l;
}
template <class P> std::pair<std::vector<float>, std::vector<float>> run2(P& p, std::vector<float> l, std::vector<float> r) {
    for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); }
    return {l, r};
}
inline std::vector<float> sine(double rmsDbfs, double seconds, double f = 1000, double fs = kFs) {
    const double a = std::pow(10.0, (rmsDbfs + 3.0103) / 20); const int n = static_cast<int>(seconds * fs);
    std::vector<float> x(static_cast<size_t>(n)); for (int i = 0; i < n; ++i) x[static_cast<size_t>(i)] = static_cast<float>(a * std::sin(2 * kPi * f * i / fs)); return x;
}
inline std::vector<float> noise(double rmsDbfs, double seconds, unsigned seed = 1) {
    Gauss nz(seed); const double a = std::pow(10.0, rmsDbfs / 20); std::vector<float> x(static_cast<size_t>(seconds * kFs)); for (auto& v : x) v = static_cast<float>(a * nz.gauss()); return x;
}
inline double rmsDb(const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += static_cast<double>(x[i]) * x[i]; return 10 * std::log10(s / static_cast<double>(b - a) + 1e-20); }
inline double rmsDb(const std::vector<float>& x) { return rmsDb(x, x.size() / 2, x.size()); }
inline double peakDb(const std::vector<float>& x, size_t a, size_t b) { double p = 0; for (size_t i = a; i < b; ++i) p = std::max(p, static_cast<double>(std::abs(x[i]))); return 20 * std::log10(p + 1e-12); }
// amplitude of the component at f over [a, b) (Hann window), in dBFS
inline double binDb(const std::vector<float>& y, double f, size_t a, size_t b, double fs = kFs) {
    std::complex<double> acc; double w = 0;
    for (size_t i = a; i < b; ++i) { const double h = 0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(i - a) / static_cast<double>(b - a - 1)); acc += h * static_cast<double>(y[i]) * std::exp(std::complex<double>(0, -2 * kPi * f * static_cast<double>(i) / fs)); w += h; }
    return 20 * std::log10(2 * std::abs(acc) / w + 1e-12);
}
inline double harmDb(const std::vector<float>& y, double f, int k) { return binDb(y, f * k, y.size() / 2, y.size()) - binDb(y, f, y.size() / 2, y.size()); }
}  // namespace tu
