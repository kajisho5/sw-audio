#include "doctest.h"
#include "ms04/ms04.hpp"
#include <cmath>
#include <complex>
#include <vector>
using namespace sw;
using namespace sw::ms04;
namespace {
const double kPi = 3.14159265358979323846, kFs = 48000.0;
std::vector<float> run(Processor& p, std::vector<float> l) { std::vector<float> r = l; for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } return l; }
std::vector<float> sine(double amp, double f, int n) { std::vector<float> x(static_cast<size_t>(n)); for (int i = 0; i < n; ++i) x[static_cast<size_t>(i)] = static_cast<float>(amp * std::sin(2 * kPi * f * i / kFs)); return x; }
double amp(const std::vector<float>& y, double f) { const size_t n0 = y.size() / 2, n = y.size() - n0; std::complex<double> a; double w = 0; for (size_t i = 0; i < n; ++i) { double h = 0.5 - 0.5 * std::cos(2 * kPi * i / (n - 1)); a += h * static_cast<double>(y[n0 + i]) * std::exp(std::complex<double>(0, -2 * kPi * f * (n0 + i) / kFs)); w += h; } return 2 * std::abs(a) / w; }
}

TEST_CASE("MS04 parameter table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Drive].max == 24.0); CHECK(s[Drive].def == 0.0);
    CHECK(s[Ceiling].def == doctest::Approx(-0.3)); CHECK(s[Knee].def == 50.0);
    CHECK(s[Oversample].steps == std::vector<double>{4, 8, 16}); CHECK(s[Oversample].def == 8.0);
    CHECK(s[GainMatch].def == 1.0); CHECK(s[Mix].def == 100.0);
}
TEST_CASE("clip curve: bounded by the ceiling, unity for small signals, hard knee is an exact clamp") {
    for (double k : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        for (double u = -20; u <= 20; u += 0.01) REQUIRE(std::abs(clipCurve(u, k)) <= 1.0 + 1e-12);
        CHECK(clipCurve(0.01, k) == doctest::Approx(0.01).epsilon(0.002));
    }
    CHECK(clipCurve(3.0, 0.0) == 1.0); CHECK(clipCurve(0.9, 0.0) == doctest::Approx(0.9));
}
TEST_CASE("latency is the FIR oversampler delay for every factor") {
    for (double f : {4.0, 8.0, 16.0}) { Processor p; p.setParam(Oversample, f); p.prepare(kFs, 256); CHECK(p.latencySamples() == 48); }
}
TEST_CASE("quiet material passes at unity") {
    Processor p; p.prepare(kFs, 256); p.snapToTargets();
    CHECK(20 * std::log10(amp(run(p, sine(0.1, 1000, 24000)), 1000) / 0.1) == doctest::Approx(0.0).epsilon(0.01));
}
TEST_CASE("Gain match: +12 dB of drive does not change the level of quiet material; off adds 12 dB") {
    Processor on; on.setParam(Drive, 12); on.prepare(kFs, 256); on.snapToTargets();
    CHECK(std::abs(20 * std::log10(amp(run(on, sine(0.01, 1000, 24000)), 1000) / 0.01)) < 0.05);
    Processor off; off.setParam(Drive, 12); off.setParam(GainMatch, 0); off.prepare(kFs, 256); off.snapToTargets();
    CHECK(20 * std::log10(amp(run(off, sine(0.01, 1000, 24000)), 1000) / 0.01) == doctest::Approx(12.0).epsilon(0.01));
}
TEST_CASE("a hot signal is held near the ceiling (FIR ringing allowed: +0.5 dB)") {
    Processor p; p.setParam(Drive, 12); p.setParam(GainMatch, 0); p.prepare(kFs, 256); p.snapToTargets();
    const auto y = run(p, sine(0.9, 997, 24000));
    double pk = 0; for (size_t i = 2000; i < y.size(); ++i) pk = std::max(pk, (double)std::abs(y[i]));
    CHECK(20 * std::log10(pk) <= -0.3 + 0.5);
}
TEST_CASE("16x aliases less than 4x on a hard-clipped 7.1 kHz tone") {
    auto alias = [](double f) {
        Processor p; p.setParam(Oversample, f); p.setParam(Knee, 0); p.setParam(Drive, 18); p.setParam(GainMatch, 0); p.prepare(kFs, 256); p.snapToTargets();
        const auto y = run(p, sine(0.5, 7100, 48000));
        // 3rd harmonic 21.3 kHz is legal; the 5th (35.5 kHz) folds to 12.5 kHz: measure that alias
        return amp(y, 48000 - 35500) / amp(y, 7100);
    };
    CHECK(alias(16) < alias(4) * 0.3);
}
TEST_CASE("MS04 Listen plays only what the clipper removed") {
    Processor quiet; quiet.setParam(Listen, 1); quiet.prepare(kFs, 256); quiet.snapToTargets();
    CHECK(20 * std::log10(amp(run(quiet, sine(0.05, 1000, 24000)), 1000) / 0.05) < -60.0);  // nothing clipped -> silence
    Processor hot; hot.setParam(Listen, 1); hot.setParam(Knee, 0); hot.setParam(Drive, 12); hot.prepare(kFs, 256); hot.snapToTargets();
    CHECK(20 * std::log10(amp(run(hot, sine(0.5, 1000, 24000)), 1000) / 0.5) > -30.0);     // clipped peaks -> audible
}
