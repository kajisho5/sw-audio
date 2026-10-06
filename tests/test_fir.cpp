#include "doctest.h"
#include "sw/fft.hpp"
#include "sw/fir_design.hpp"
#include "sw/convolver.hpp"
#include <cmath>
#include <complex>
#include <random>
#include <vector>
using namespace sw;
namespace { const double kPi = 3.14159265358979323846; using cd = std::complex<double>; }

TEST_CASE("FFT matches a direct DFT and inverts exactly") {
    for (int n : {8, 64, 1024}) {
        std::mt19937 rng(n); std::normal_distribution<double> nd(0, 1);
        std::vector<cd> x(static_cast<size_t>(n)); for (auto& v : x) v = cd(nd(rng), nd(rng));
        Fft f(n); std::vector<cd> X = x; f.forward(X);
        double err = 0;
        for (int k = 0; k < n; ++k) { cd s; for (int t = 0; t < n; ++t) s += x[static_cast<size_t>(t)] * std::exp(cd(0, -2 * kPi * k * t / n)); err = std::max(err, std::abs(s - X[static_cast<size_t>(k)])); }
        CHECK(err < 1e-9 * n);
        f.inverse(X); double back = 0; for (int t = 0; t < n; ++t) back = std::max(back, std::abs(X[static_cast<size_t>(t)] - x[static_cast<size_t>(t)]));
        CHECK(back < 1e-12 * n);
    }
}
TEST_CASE("analog band magnitudes: bell peak, shelf plateaus, cut -3 dB") {
    BandShape b{BandShape::Bell, 1000, 6, 1.0, 12};
    CHECK(20 * std::log10(b.magnitude(1000)) == doctest::Approx(6.0));
    CHECK(20 * std::log10(BandShape{BandShape::LowShelf, 100, 6, 0.7071, 12}.magnitude(5)) == doctest::Approx(6.0).epsilon(0.01));
    CHECK(20 * std::log10(BandShape{BandShape::HighShelf, 5000, -6, 0.7071, 12}.magnitude(40000)) == doctest::Approx(-6.0).epsilon(0.01));
    CHECK(20 * std::log10(BandShape{BandShape::LowCut, 100, 0, 0.7071, 12}.magnitude(100)) == doctest::Approx(-3.01).epsilon(0.01));
    CHECK(20 * std::log10(BandShape{BandShape::LowCut, 100, 0, 0.7071, 24}.magnitude(50)) == doctest::Approx(-24.1).epsilon(0.02));
}
TEST_CASE("linear-phase kernel: symmetric, delay L/2, magnitude matches the target") {
    const int L = 2048; const double fs = 48000;
    std::vector<BandShape> bands = {{BandShape::Bell, 1000, 6, 1.0, 12}, {BandShape::HighShelf, 8000, -4, 0.7071, 12}};
    const auto h = designKernel(bands, {}, L, fs);
    REQUIRE(static_cast<int>(h.size()) == L);
    double asym = 0; for (int k = 1; k < L / 2; ++k) asym = std::max(asym, std::abs(h[static_cast<size_t>(L / 2 - k)] - h[static_cast<size_t>(L / 2 + k)]));
    CHECK(asym < 1e-12);
    for (double f : {1000.0, 3000.0, 12000.0}) {
        cd H; for (int n = 0; n < L; ++n) H += h[static_cast<size_t>(n)] * std::exp(cd(0, -2 * kPi * f * n / fs));
        double want = 1; for (auto& b : bands) want *= b.magnitude(f);
        CHECK(20 * std::log10(std::abs(H)) == doctest::Approx(20 * std::log10(want)).epsilon(0.01));
    }
}
TEST_CASE("minimum-phase part: same magnitude, energy right after the delay instead of before it") {
    const int L = 2048; const double fs = 48000;
    std::vector<BandShape> mn = {{BandShape::Bell, 2000, 9, 4.0, 12}};
    const auto lin = designKernel(mn, {}, L, fs), mix = designKernel({}, mn, L, fs);
    auto pre = [&](const std::vector<double>& h) { double e = 0; for (int n = 0; n < L / 2 - 2; ++n) e += h[static_cast<size_t>(n)] * h[static_cast<size_t>(n)]; return e; };
    CHECK(pre(mix) < pre(lin) * 1e-3);
    cd H; for (int n = 0; n < L; ++n) H += mix[static_cast<size_t>(n)] * std::exp(cd(0, -2 * kPi * 2000.0 * n / fs));
    CHECK(20 * std::log10(std::abs(H)) == doctest::Approx(9.0).epsilon(0.01));
}
TEST_CASE("partitioned convolver equals direct convolution, delayed by one block") {
    const int L = 512, B = 64; std::mt19937 rng(7); std::normal_distribution<double> nd(0, 1);
    std::vector<double> h(L); for (auto& v : h) v = nd(rng) * 0.05;
    Convolver cv; cv.prepare(L, B, 1); cv.setKernel(h, true);
    std::vector<float> x(4000); for (auto& v : x) v = static_cast<float>(nd(rng));
    std::vector<float> y = x;
    for (size_t off = 0; off < y.size(); off += 37) { const int n = static_cast<int>(std::min<size_t>(37, y.size() - off)); float* c[1] = {y.data() + off}; cv.process(c, 1, n); }
    CHECK(cv.latencySamples() == B);
    double err = 0;
    for (int t = B + L; t < 4000; ++t) { double s = 0; for (int k = 0; k < L; ++k) s += h[static_cast<size_t>(k)] * x[static_cast<size_t>(t - B - k)]; err = std::max(err, std::abs(s - y[static_cast<size_t>(t)])); }
    CHECK(err < 1e-4);
}
TEST_CASE("kernel changes crossfade without a jump") {
    const int L = 256, B = 64;
    Convolver cv; cv.prepare(L, B, 1);
    std::vector<double> a(L, 0.0), b(L, 0.0); a[0] = 1.0; b[0] = -1.0;  // polarity flip: worst case
    cv.setKernel(a, true);
    std::vector<float> x(48000); for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(std::sin(2 * kPi * 100.0 * i / 48000.0));
    std::vector<float> y = x;
    for (size_t off = 0; off < y.size(); off += 64) { if (off == 9600) cv.setKernel(b, false); float* c[1] = {y.data() + off}; cv.process(c, 1, 64); }
    double jump = 0; for (size_t i = 9601; i < 12000; ++i) jump = std::max(jump, (double)std::abs(y[i] - y[i - 1]));
    CHECK(jump < 0.03);  // a sine at 100 Hz moves at most 0.013 per sample; an instant flip would jump ~2
}
