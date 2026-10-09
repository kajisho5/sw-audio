#include "doctest.h"
#include "sw/dynamics.hpp"
#include <cmath>
#include <random>
#include <vector>
using namespace sw;
namespace {
const double kPi = 3.14159265358979323846, kFs = 48000.0;
// reference true peak: 32x windowed-sinc interpolation (slow, test only)
double refTruePeak(const std::vector<float>& x) {
    const int up = 32, half = 64;
    double peak = 0;
    for (size_t n = static_cast<size_t>(half); n + static_cast<size_t>(half) < x.size(); ++n)
        for (int k = 0; k < up; ++k) {
            const double t = n + static_cast<double>(k) / up;
            double acc = 0;
            for (int m = -half; m <= half; ++m) {
                const double d = t - static_cast<double>(n + static_cast<size_t>(m) - 0) ;
                const double sinc = std::abs(d) < 1e-12 ? 1.0 : std::sin(kPi * d) / (kPi * d);
                const double w = 0.5 + 0.5 * std::cos(kPi * d / (half + 1));
                acc += x[n + static_cast<size_t>(m)] * sinc * w;
            }
            peak = std::max(peak, std::abs(acc));
        }
    return peak;
}
double db(double a) { return 20 * std::log10(a); }
}

TEST_CASE("gain computer: hard knee") {
    GainComputer g; g.set(-20.0, 4.0, 0.0);
    CHECK(g.gainDb(-30.0) == doctest::Approx(0.0));
    CHECK(g.gainDb(-10.0) == doctest::Approx(-7.5));
}
TEST_CASE("gain computer: soft knee is continuous and quadratic in the knee") {
    GainComputer g; g.set(-20.0, 4.0, 10.0);
    CHECK(g.gainDb(-25.0) == doctest::Approx(0.0));
    CHECK(g.gainDb(-20.0) == doctest::Approx(-0.75 * 25.0 / 20.0));
    CHECK(g.gainDb(-15.0) == doctest::Approx(-0.75 * 5.0));
    CHECK(g.gainDb(-15.0001) == doctest::Approx(g.gainDb(-14.9999)).epsilon(1e-3));
}
TEST_CASE("gain computer: infinite ratio limits") {
    GainComputer g; g.set(-6.0, GainComputer::kInfinity, 0.0);
    CHECK(g.gainDb(0.0) == doctest::Approx(-6.0));
}
TEST_CASE("ballistics: attack reaches 63 % of a step in the attack time, release likewise") {
    Ballistics b; b.set(kFs, 10.0, 100.0);
    double v = 0;
    for (int i = 0; i < 480; ++i) v = b.process(-10.0);
    CHECK(v == doctest::Approx(-6.32).epsilon(0.01));
    for (int i = 0; i < 48000; ++i) v = b.process(-10.0);
    for (int i = 0; i < 4800; ++i) v = b.process(0.0);
    CHECK(v == doctest::Approx(-3.68).epsilon(0.01));
}
TEST_CASE("RMS detector reads a sine 3 dB below its peak") {
    LevelDetector d; d.set(kFs, LevelDetector::Mode::Rms);
    double l = 0;
    for (int i = 0; i < 48000; ++i) l = d.process(0.5 * std::sin(2 * kPi * 1000.0 * i / kFs));
    CHECK(db(l) == doctest::Approx(db(0.5) - 3.01).epsilon(0.01));
}
TEST_CASE("true peak detector finds an inter-sample peak (fs/4, 45 degrees)") {
    TruePeakDetector tp; tp.setup(8);
    double peak = 0;
    for (int i = 0; i < 4800; ++i) peak = std::max(peak, tp.process(std::sin(2 * kPi * 0.25 * i + kPi / 4)));
    CHECK(db(peak) == doctest::Approx(0.0).epsilon(0.1));       // samples only reach -3 dB
    CHECK(tp.latencySamples() > 0);
}
TEST_CASE("peak limiter: sample peaks never exceed the ceiling") {
    PeakLimiter lim; lim.prepare(kFs, 2, 72, false, 4);
    lim.set(-1.0, 50.0, 1.0);
    std::mt19937 rng(1); std::normal_distribution<double> n(0, 0.3); std::uniform_real_distribution<double> u(0, 1);
    const double ceil = std::pow(10.0, -1.0 / 20.0);
    double peak = 0;
    std::vector<float> l(256), r(256);
    for (int blk = 0; blk < 2000; ++blk) {
        for (int i = 0; i < 256; ++i) { const double spike = u(rng) < 0.002 ? 16.0 : 1.0; l[i] = static_cast<float>(n(rng) * spike); r[i] = static_cast<float>(n(rng)); }
        float* c[2] = {l.data(), r.data()}; lim.process(c, 2, 256);
        for (int i = 0; i < 256; ++i) peak = std::max(peak, (double)std::max(std::abs(l[i]), std::abs(r[i])));
    }
    CHECK(peak <= ceil + 1e-6);
}
TEST_CASE("peak limiter: signals below the ceiling pass unchanged, delayed by the reported latency") {
    PeakLimiter lim; lim.prepare(kFs, 2, 72, false, 4); lim.set(-1.0, 50.0, 1.0);
    const int L = lim.latencySamples();
    CHECK(L == 72);
    std::vector<float> l(4096), r(4096), in(4096);
    std::mt19937 rng(2); std::normal_distribution<double> n(0, 0.05);
    for (int i = 0; i < 4096; ++i) in[i] = l[i] = r[i] = static_cast<float>(n(rng));
    float* c[2] = {l.data(), r.data()}; lim.process(c, 2, 4096);
    for (int i = L; i < 4096; ++i) REQUIRE(l[i] == doctest::Approx(in[i - L]).epsilon(1e-6));
}
TEST_CASE("peak limiter Auto release: short reductions recover fast, sustained ones (more than 100 ms below -1 dB) slowly, whatever the block size") {
    auto recoveryMs = [](double burstMs, int block) {
        PeakLimiter lim; lim.prepare(kFs, 2, 72, false, 4); lim.set(-1.0, 30.0, 1.0); lim.setAutoRelease(30.0, 300.0);
        const int burst = static_cast<int>(burstMs * 0.001 * kFs), total = burst + static_cast<int>(1.5 * kFs);
        std::vector<float> l(static_cast<size_t>(total)), r;
        for (int i = 0; i < total; ++i) l[static_cast<size_t>(i)] = i < burst ? 2.0f : 0.3f;
        r = l;
        for (int off = 0; off < total; off += block) { const int n = std::min(block, total - off); float* c[2] = {l.data() + off, r.data() + off}; lim.process(c, 2, n); }
        // the output is 0.3 x the gain once the burst is over (and the lookahead has passed): the time until the gain is back above 0.9
        for (int i = burst + 200; i < total; ++i) if (l[static_cast<size_t>(i)] > 0.27f) return (i - burst) * 1000.0 / kFs;
        return 9999.0;
    };
    const double shortMs = recoveryMs(30.0, 256), longMs = recoveryMs(500.0, 256);
    CHECK(shortMs < 120.0); CHECK(longMs > 3.0 * shortMs); CHECK(longMs > 300.0);
    CHECK(recoveryMs(30.0, 1) == doctest::Approx(shortMs)); CHECK(recoveryMs(500.0, 1) == doctest::Approx(longMs)); CHECK(recoveryMs(500.0, 37) == doctest::Approx(longMs));
}
TEST_CASE("true peak limiter keeps the true peak at the ceiling (reference 32x)") {
    PeakLimiter lim; lim.prepare(kFs, 2, 72, true, 8); lim.set(-1.0, 50.0, 1.0);
    std::vector<float> l(24000), r(24000);
    for (int i = 0; i < 24000; ++i) l[i] = r[i] = static_cast<float>(1.6 * std::sin(2 * kPi * 11025.0 * i / 44100.0 + 0.8) * (0.6 + 0.4 * std::sin(i * 0.001)));
    float* c[2] = {l.data(), r.data()}; lim.process(c, 2, 24000);
    std::vector<float> tail(l.begin() + 4000, l.end());
    CHECK(db(refTruePeak(tail)) <= -1.0 + 0.1);
}
