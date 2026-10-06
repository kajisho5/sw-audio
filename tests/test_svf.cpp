#include "doctest.h"
#include "sw/svf.hpp"
#include <cmath>
#include <complex>
#include <random>
#include <utility>
#include <vector>
using namespace sw;
using cd = std::complex<double>;

namespace {
const double kPi = 3.14159265358979323846;
// Measure the real filter: impulse response -> DFT at f (dB)
template <class F> double measureDb(F& f, double freq, double fs, int n = 1 << 17) {
    cd acc{0, 0};
    for (int i = 0; i < n; ++i) {
        double y = f.process(i == 0 ? 1.0 : 0.0);
        acc += y * std::exp(cd(0, -2 * kPi * freq * i / fs));
    }
    return 20 * std::log10(std::abs(acc));
}
// Analog prototype evaluated at the bilinear-warped frequency (exact for TPT SVF)
cd S(double f, double fc, double fs) { return cd(0, std::tan(kPi * f / fs) / std::tan(kPi * fc / fs)); }
double db(cd h) { return 20 * std::log10(std::abs(h)); }
const double kFs = 48000.0;
const double kTestFreqs[] = {25, 100, 440, 1000, 3150, 8000, 15000, 20000};
}

TEST_CASE("bell matches analog prototype at all test frequencies") {
    Svf f; f.setup(Svf::Mode::Bell, 1000, kFs, 1.4, 9.0);
    const double A = std::pow(10.0, 9.0 / 40), Q = 1.4;
    for (double fr : kTestFreqs) {
        f.reset();
        cd s = S(fr, 1000, kFs);
        CHECK(measureDb(f, fr, kFs) == doctest::Approx(db((s * s + s * A / Q + 1.0) / (s * s + s / (A * Q) + 1.0))).epsilon(1e-4));
    }
}
TEST_CASE("bell peak equals the gain setting at fc") {
    Svf f; f.setup(Svf::Mode::Bell, 2000, kFs, 1.0, -12.0);
    CHECK(measureDb(f, 2000, kFs) == doctest::Approx(-12.0).epsilon(1e-4));
}
TEST_CASE("low shelf matches analog prototype") {
    Svf f; f.setup(Svf::Mode::LowShelf, 100, kFs, 0.7071, 6.0);
    const double A = std::pow(10.0, 6.0 / 40), Q = 0.7071;
    for (double fr : kTestFreqs) {
        f.reset();
        cd s = S(fr, 100, kFs);
        CHECK(measureDb(f, fr, kFs) == doctest::Approx(db(A * (s * s + std::sqrt(A) / Q * s + A) / (A * s * s + std::sqrt(A) / Q * s + 1.0))).epsilon(1e-4));
    }
}
TEST_CASE("high shelf matches analog prototype") {
    Svf f; f.setup(Svf::Mode::HighShelf, 8000, kFs, 0.7071, -7.5);
    const double A = std::pow(10.0, -7.5 / 40), Q = 0.7071;
    for (double fr : kTestFreqs) {
        f.reset();
        cd s = S(fr, 8000, kFs);
        CHECK(measureDb(f, fr, kFs) == doctest::Approx(db(A * (A * s * s + std::sqrt(A) / Q * s + 1.0) / (s * s + std::sqrt(A) / Q * s + A))).epsilon(1e-4));
    }
}
TEST_CASE("2nd order low pass and high pass match Butterworth prototypes") {
    Svf lp; lp.setup(Svf::Mode::LowPass, 12000, kFs, 0.70710678, 0);
    Svf hp; hp.setup(Svf::Mode::HighPass, 80, kFs, 0.70710678, 0);
    for (double fr : kTestFreqs) {
        lp.reset(); hp.reset();
        cd s1 = S(fr, 12000, kFs), s2 = S(fr, 80, kFs);
        CHECK(measureDb(lp, fr, kFs) == doctest::Approx(db(1.0 / (s1 * s1 + s1 / 0.70710678 + 1.0))).epsilon(1e-4));
        CHECK(measureDb(hp, fr, kFs) == doctest::Approx(db(s2 * s2 / (s2 * s2 + s2 / 0.70710678 + 1.0))).epsilon(1e-4));
    }
}
TEST_CASE("1st order low pass and high pass match prototypes") {
    OnePole lp; lp.setup(OnePole::Mode::LowPass, 2000, kFs);
    OnePole hp; hp.setup(OnePole::Mode::HighPass, 40, kFs);
    for (double fr : kTestFreqs) {
        lp.reset(); hp.reset();
        cd s1 = S(fr, 2000, kFs), s2 = S(fr, 40, kFs);
        CHECK(measureDb(lp, fr, kFs) == doctest::Approx(db(1.0 / (s1 + 1.0))).epsilon(1e-4));
        CHECK(measureDb(hp, fr, kFs) == doctest::Approx(db(s2 / (s2 + 1.0))).epsilon(1e-4));
    }
}
TEST_CASE("bell stays bounded when cutoff is swept every sample") {
    Svf f;
    std::mt19937 rng(1);
    std::uniform_real_distribution<double> d(-1, 1);
    double peak = 0;
    for (int i = 0; i < 200000; ++i) {
        double fc = 20.0 * std::pow(1000.0, 0.5 + 0.5 * std::sin(i * 0.01));
        f.setup(Svf::Mode::Bell, fc, kFs, 3.0, 15.0);
        double y = f.process(d(rng));
        REQUIRE(std::isfinite(y));
        peak = std::max(peak, std::abs(y));
    }
    CHECK(peak < 50.0);
}

TEST_CASE("setupRamp ends exactly on the same response as setup") {
    Svf a, b;
    a.setup(Svf::Mode::LowShelf, 100, kFs, 0.7071, -15);
    b.setup(Svf::Mode::LowShelf, 100, kFs, 0.7071, -15);
    b.setupRamp(Svf::Mode::LowShelf, 100, kFs, 0.7071, 15, 16);
    for (int i = 0; i < 16; ++i) b.process(0.0);
    a.setup(Svf::Mode::LowShelf, 100, kFs, 0.7071, 15);
    a.reset(); b.reset();
    CHECK(measureDb(b, 60, kFs) == doctest::Approx(measureDb(a, 60, kFs)).epsilon(1e-9));
}
TEST_CASE("ramping a shelf by 30 dB in 16-sample segments does not zipper") {
    // 2nd difference exposes stair-step coefficient changes; a 60 Hz sine has a tiny one
    // mode 0: stepped every 16 samples, 1: setupRamp over 16 samples, 2: exact setup every sample (ideal)
    auto run = [](int mode) {
        Svf f; f.setup(Svf::Mode::LowShelf, 100, kFs, 0.7071, -15);
        double worst = 0, y1 = 0, y2 = 0, endPeak = 0;
        for (int i = 0; i < 4800; ++i) {
            if (i >= 960 && i < 1920) {
                if (mode == 2) f.setup(Svf::Mode::LowShelf, 100, kFs, 0.7071, -15 + 30.0 * (i - 960 + 1) / 960.0);
                else if (i % 16 == 0) {
                    const double g = -15 + 30.0 * (i - 960 + 16) / 960.0;
                    if (mode == 1) f.setupRamp(Svf::Mode::LowShelf, 100, kFs, 0.7071, g, 16);
                    else f.setup(Svf::Mode::LowShelf, 100, kFs, 0.7071, g);
                }
            }
            const double y = f.process(0.2 * std::sin(2 * 3.141592653589793 * 60.0 * i / kFs));
            if (i > 2 && i < 2400) worst = std::max(worst, std::abs(y - 2 * y1 + y2));
            if (i > 3600) endPeak = std::max(endPeak, std::abs(y));
            y2 = y1; y1 = y;
        }
        return std::make_pair(worst, endPeak);
    };
    const auto stepped = run(0), ramped = run(1), exact = run(2);
    CHECK(ramped.second == doctest::Approx(stepped.second).epsilon(0.01));  // both reach +15 dB
    CHECK(ramped.first < stepped.first / 10.0);
    CHECK(ramped.first <= 1.2 * exact.first);  // as smooth as an exact per-sample update
}

TEST_CASE("OnePole setupRamp ends on the target and is as smooth as per-sample updates") {
    auto run = [](int mode) {
        OnePole f; f.setup(OnePole::Mode::HighPass, 40, kFs);
        double worst = 0, y1 = 0, y2 = 0;
        for (int i = 0; i < 4800; ++i) {
            const auto hz = [](int j) { return 40.0 * std::pow(5.0, (j - 960) / 960.0); };
            if (i >= 960 && i < 1920) {
                if (mode == 2) f.setup(OnePole::Mode::HighPass, hz(i + 1), kFs);
                else if (i % 16 == 0) {
                    if (mode == 1) f.setupRamp(OnePole::Mode::HighPass, hz(i + 16), kFs, 16);
                    else f.setup(OnePole::Mode::HighPass, hz(i + 16), kFs);
                }
            }
            const double y = f.process(0.5 * std::sin(2 * 3.141592653589793 * 150.0 * i / kFs));
            if (i > 2 && i < 2400) worst = std::max(worst, std::abs(y - 2 * y1 + y2));
            y2 = y1; y1 = y;
        }
        return worst;
    };
    CHECK(run(1) <= 1.2 * run(2));
    OnePole a, b;
    b.setup(OnePole::Mode::HighPass, 40, kFs); b.setupRamp(OnePole::Mode::HighPass, 200, kFs, 16);
    for (int i = 0; i < 16; ++i) b.process(0.0);
    a.setup(OnePole::Mode::HighPass, 200, kFs); a.reset(); b.reset();
    CHECK(measureDb(b, 100, kFs) == doctest::Approx(measureDb(a, 100, kFs)).epsilon(1e-9));
}
