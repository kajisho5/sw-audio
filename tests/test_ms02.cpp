#include "doctest.h"
#include "ms02/ms02.hpp"
#include "sw/svf.hpp"
#include <cmath>
#include <random>
#include <vector>
using namespace sw;
using namespace sw::ms02;
namespace {
const double kPi = 3.14159265358979323846, kFs = 48000.0;
double refTruePeakDb(const std::vector<float>& x) {  // 32x windowed sinc (test reference)
    const int up = 32, half = 48; double peak = 0;
    for (size_t n = half; n + half < x.size(); ++n)
        for (int k = 0; k < up; ++k) {
            const double t = n + static_cast<double>(k) / up; double acc = 0;
            for (int m = -half; m <= half; ++m) {
                const double d = t - static_cast<double>(static_cast<long>(n) + m);
                const double sinc = std::abs(d) < 1e-12 ? 1.0 : std::sin(kPi * d) / (kPi * d);
                acc += x[static_cast<size_t>(static_cast<long>(n) + m)] * sinc * (0.5 + 0.5 * std::cos(kPi * d / (half + 1)));
            }
            peak = std::max(peak, std::abs(acc));
        }
    return 20 * std::log10(peak);
}
std::vector<float> runBlock(Processor& p, std::vector<float> l) {
    std::vector<float> r = l;
    for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); }
    return l;
}
}

TEST_CASE("MS02 parameter table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Gain].max == 24.0); CHECK(s[Ceiling].def == -1.0); CHECK(std::string(s[Ceiling].unit) == "dBTP");
    CHECK(std::string(s[Release].maxLabel) == "Auto"); CHECK(s[Release].def == s[Release].max);
    CHECK(s[LookaheadMs].min == 0.5); CHECK(s[LookaheadMs].max == 5.0); CHECK(s[LookaheadMs].def == 1.5);
    CHECK(s[TruePeak].def == 1.0); CHECK(s[Isp].steps == std::vector<double>{4, 8}); CHECK(s[Isp].def == 8.0);
    CHECK(s[Link].def == 100.0); CHECK(s[Dither].steps == std::vector<double>{0, 16, 24});
}
TEST_CASE("latency is lookahead + interpolation delay + margin (true peak on) and just lookahead (off)") {
    Processor p; p.prepare(kFs, 256);
    CHECK(p.latencySamples() == 72 + 8 + 8);
    p.setParam(TruePeak, 0); p.prepare(kFs, 256);
    CHECK(p.latencySamples() == 72);
}
// Requirement scope: material band-limited to 20 kHz (music-like). Full-band noise with strong energy at
// 22-24 kHz under heavy limiting can read up to ~0.4 dB higher on an ideal reconstruction (documented).
TEST_CASE("+12 dB of gain into -1 dBTP keeps the true peak at the ceiling (content up to 20 kHz)") {
    Processor p; p.setParam(Gain, 12.0); p.prepare(kFs, 256); p.snapToTargets();
    std::mt19937 rng(4); std::normal_distribution<double> nd(0, 0.15);
    Svf lp[4]; const double qs[4] = {0.5098, 0.6013, 0.9000, 2.5629};  // 8th-order Butterworth, 20 kHz
    for (int k = 0; k < 4; ++k) lp[k].setup(Svf::Mode::LowPass, 20000.0, kFs, qs[k], 0);
    std::vector<float> x(24000);
    for (size_t i = 0; i < x.size(); ++i) { double v = nd(rng); for (auto& f : lp) v = f.process(v); x[i] = static_cast<float>(v + 0.3 * std::sin(2 * kPi * 11000.0 * i / kFs)); }
    const auto y = runBlock(p, x);
    CHECK(refTruePeakDb(std::vector<float>(y.begin() + 4000, y.end())) <= -1.0 + 0.1);
}
TEST_CASE("quiet material passes untouched apart from the latency") {
    Processor p; p.prepare(kFs, 256); p.snapToTargets();
    std::vector<float> x(4000);
    for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(0.1 * std::sin(2 * kPi * 440.0 * i / kFs));
    const auto y = runBlock(p, x);
    const int L = p.latencySamples();
    for (size_t i = 1000; i < x.size(); ++i) REQUIRE(y[i] == doctest::Approx(x[i - L]).epsilon(1e-5));
}
TEST_CASE("16-bit dither quantizes the output to 16-bit steps") {
    Processor p; p.setParam(Dither, 16); p.prepare(kFs, 256); p.snapToTargets();
    std::vector<float> x(2048);
    for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(0.2 * std::sin(i * 0.01));
    for (float v : runBlock(p, x)) { const double q = v * 32768.0; REQUIRE(std::abs(q - std::round(q)) < 1e-3); }
}
TEST_CASE("random automation stays finite and under the ceiling (sample peak)") {
    Processor p; p.prepare(44100.0, 128);
    std::mt19937 rng(10); std::uniform_real_distribution<double> u(0, 1); std::uniform_real_distribution<float> d(-1, 1);
    std::vector<float> l(128), r(128); bool ok = true;
    for (int b = 0; b < 2000; ++b) {
        for (int id = 0; id < kNumParams; ++id) if (u(rng) < 0.1 && id != LookaheadMs && id != TruePeak && id != Isp) p.setParam(id, specs()[static_cast<size_t>(id)].toValue(u(rng)));
        for (int i = 0; i < 128; ++i) { l[i] = d(rng) * 3; r[i] = d(rng); }
        float* c[2] = {l.data(), r.data()}; p.process(c, 2, 128);
        for (int i = 0; i < 128; ++i) if (!std::isfinite(l[i]) || std::abs(l[i]) > 1.0001f) ok = false;
    }
    CHECK(ok);
}
