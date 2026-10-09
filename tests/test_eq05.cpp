#include "doctest.h"
#include "os_helpers.hpp"
#include "eq05/eq05.hpp"
#include <cmath>
#include <complex>
#include <random>
#include <vector>
using namespace sw;
using namespace sw::eq05;
namespace {
const double kPi = 3.14159265358979323846, kFs = 48000.0;
double dB(double a) { return 20 * std::log10(a); }
// steady-state gain (dB) of the processor for a low-level sine (saturation negligible)
double gainDb(const std::vector<std::pair<int, double>>& settings, double f, double level = 0.001) {
    Processor p; p.prepare(kFs, 512);
    p.setParam(Drive, 0.0);
    for (auto& s : settings) p.setParam(s.first, s.second);
    p.snapToTargets();
    const int n = 48000;
    std::vector<float> l(n), r(n);
    for (int i = 0; i < n; ++i) l[i] = r[i] = static_cast<float>(level * std::sin(2 * kPi * f * i / kFs));
    float* ch[2] = {l.data(), r.data()};
    for (int off = 0; off < n; off += 512) { float* c[2] = {ch[0] + off, ch[1] + off}; p.process(c, 2, std::min(512, n - off)); }
    std::complex<double> acc; double w = 0;
    for (int i = n / 2; i < n; ++i) { double h = 0.5 - 0.5 * std::cos(2 * kPi * (i - n / 2) / (n / 2 - 1)); acc += h * l[i] * std::exp(std::complex<double>(0, -2 * kPi * f * i / kFs)); w += h; }
    return dB(2 * std::abs(acc) / w / level);
}
double maxStep(const std::vector<float>& x, int from, int to) { double m = 0; for (int i = from + 1; i < to; ++i) m = std::max(m, (double)std::abs(x[i] - x[i - 1])); return m; }
}

TEST_CASE("parameter table follows the spec (count, ranges, defaults)") {
    const auto& s = specs();
    REQUIRE(s.size() == kNumParams);
    CHECK(kNumParams == 19);   // the 18 of the spec + the common oversampling setting (eq05.os)
    CHECK(s[HfFreq].def == 8000.0);  CHECK(s[HfFreq].min == 1500.0); CHECK(s[HfFreq].max == 16000.0);
    CHECK(s[HmfFreq].def == 2000.0); CHECK(s[HmfFreq].min == 600.0); CHECK(s[HmfFreq].max == 7000.0);
    CHECK(s[LmfFreq].def == 600.0);  CHECK(s[LmfFreq].min == 200.0); CHECK(s[LmfFreq].max == 2500.0);
    CHECK(s[LfFreq].def == 100.0);   CHECK(s[LfFreq].min == 30.0);   CHECK(s[LfFreq].max == 450.0);
    CHECK(s[HmfQ].min == 0.5); CHECK(s[HmfQ].max == 3.0); CHECK(s[HmfQ].def == 1.0);
    CHECK(s[HfGain].min == -15.0); CHECK(s[HfGain].max == 15.0); CHECK(s[HfGain].def == 0.0);
    CHECK(s[Hpf].steps == std::vector<double>{0, 40, 80, 120, 200});
    CHECK(s[Lpf].steps == std::vector<double>{0, 8000, 12000, 16000, 20000});
    CHECK(s[Drive].def == 2.0); CHECK(s[Output].min == -10.0); CHECK(s[Output].max == 10.0);
    CHECK(s[HfShape].def == 0.0);   // 0 = Shelf
    CHECK(s[DrivePos].def == 1.0);  // 1 = Post
    CHECK(s[In].def == 1.0);
    CHECK(s[HfFreq].curve == Curve::Log); CHECK(s[HfGain].curve == Curve::Lin); CHECK(s[Hpf].curve == Curve::Step);
}
TEST_CASE("flat settings give a flat response within 0.05 dB from 20 Hz to 16 kHz") {
    for (double f : {20.0, 100.0, 1000.0, 5000.0, 10000.0, 16000.0})
        CHECK(std::abs(gainDb({}, f)) < 0.05);
}
TEST_CASE("HF shelf +6 dB reaches about +6 dB at 16 kHz") {
    // analog high shelf (8 kHz, Q 0.707, +6 dB) at 16 kHz / 48 kHz = +5.92 dB; allow 0.1 dB incl. oversampler ripple
    CHECK(std::abs(gainDb({{HfGain, 6.0}}, 16000.0) - 5.92) < 0.1);
}
TEST_CASE("HMF bell -9 dB at 2 kHz is -9 dB at its centre") {
    CHECK(std::abs(gainDb({{HmfGain, -9.0}, {HmfFreq, 2000.0}, {HmfQ, 2.0}}, 2000.0) + 9.0) < 0.05);
}
TEST_CASE("HPF 80 is 18 dB/oct: -3 dB at 80 Hz and about -18 dB at 40 Hz") {
    CHECK(std::abs(gainDb({{Hpf, 80.0}}, 80.0) + 3.01) < 0.05);
    CHECK(std::abs(gainDb({{Hpf, 80.0}}, 40.0) + 18.13) < 0.1);
}
TEST_CASE("LPF 12k is -3 dB at 12 kHz") {
    CHECK(std::abs(gainDb({{Lpf, 12000.0}}, 12000.0) + 3.01) < 0.05);
}
TEST_CASE("left and right are processed independently") {
    Processor p; p.prepare(kFs, 256); p.setParam(HfGain, 12.0); p.snapToTargets();
    std::vector<float> l(256), r(256, 0.0f);
    for (int i = 0; i < 256; ++i) l[i] = static_cast<float>(0.5 * std::sin(i * 0.3));
    float* ch[2] = {l.data(), r.data()};
    p.process(ch, 2, 256);
    for (float v : r) CHECK(v == 0.0f);
}
TEST_CASE("an instant +-15 dB jump on LF gain does not click") {
    Processor p; p.prepare(kFs, 64); p.setParam(Drive, 0.0); p.setParam(LfGain, -15.0); p.snapToTargets();
    const int n = 24000; std::vector<float> l(n), r(n);
    for (int i = 0; i < n; ++i) l[i] = r[i] = static_cast<float>(0.2 * std::sin(2 * kPi * 60.0 * i / kFs));
    for (int off = 0; off < n; off += 64) {
        if (off == 9600) p.setParam(LfGain, 15.0);
        float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, 64);
    }
    // steady state after the change sets the reference slope
    CHECK(maxStep(l, 9600, 12000) <= 1.2 * maxStep(l, 18000, n));
}
TEST_CASE("switching HPF from Off to 200 Hz does not click") {
    Processor p; p.prepare(kFs, 64); p.setParam(Drive, 0.0); p.snapToTargets();
    const int n = 24000; std::vector<float> l(n), r(n);
    for (int i = 0; i < n; ++i) l[i] = r[i] = static_cast<float>(0.5 * std::sin(2 * kPi * 1000.0 * i / kFs));
    for (int off = 0; off < n; off += 64) {
        if (off == 9600) p.setParam(Hpf, 200.0);
        float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, 64);
    }
    CHECK(maxStep(l, 9600, 12000) <= 1.1 * maxStep(l, 18000, n));
}
TEST_CASE("random automation of every parameter stays finite and bounded") {
    Processor p; p.prepare(44100.0, 128);
    std::mt19937 rng(7); std::uniform_real_distribution<double> u(0, 1); std::uniform_real_distribution<float> d(-1, 1);
    std::vector<float> l(128), r(128);
    bool ok = true; double peak = 0;
    for (int blk = 0; blk < 4000; ++blk) {
        for (int id = 0; id < kNumParams; ++id) if (u(rng) < 0.2) p.setParam(id, specs()[id].toValue(u(rng)));
        for (int i = 0; i < 128; ++i) { l[i] = d(rng); r[i] = d(rng); }
        float* c[2] = {l.data(), r.data()}; p.process(c, 2, 128);
        for (int i = 0; i < 128; ++i) { if (!std::isfinite(l[i]) || !std::isfinite(r[i])) ok = false; peak = std::max(peak, (double)std::abs(l[i])); }
    }
    CHECK(ok);
    CHECK(peak < 32.0);
}
TEST_CASE("reported latency is 0 samples") {
    Processor p; p.prepare(kFs, 64);
    CHECK(p.latencySamples() == 0);
}
TEST_CASE("decaying tails never produce subnormal output samples") {
    Processor p; p.prepare(kFs, 256); p.setParam(LfGain, 12.0); p.setParam(Hpf, 40.0); p.snapToTargets();
    std::mt19937 rng(11); std::uniform_real_distribution<float> d(-1, 1);
    std::vector<float> l(256), r(256);
    int subnormals = 0;
    for (int blk = 0; blk < 800; ++blk) {
        for (int i = 0; i < 256; ++i) { l[i] = blk < 4 ? d(rng) : 0.0f; r[i] = l[i]; }
        float* c[2] = {l.data(), r.data()}; p.process(c, 2, 256);
        for (int i = 0; i < 256; ++i) if (std::fpclassify(l[i]) == FP_SUBNORMAL || std::fpclassify(r[i]) == FP_SUBNORMAL) ++subnormals;
    }
    CHECK(subnormals == 0);
}
TEST_CASE("EQ05 Drive stage has +6 dBFS headroom: Drive 0 keeps a -7 dBFS peak tone clean") {
    Processor p; p.setParam(Drive, 0); p.prepare(48000, 256); p.snapToTargets();
    std::vector<float> l(48000), r(48000);
    for (int i = 0; i < 48000; ++i) l[i] = r[i] = static_cast<float>(0.447 * std::sin(2 * 3.14159265358979323846 * 1000.0 * i / 48000.0));
    for (int off = 0; off < 48000; off += 256) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, std::min(256, 48000 - off)); }
    double pk = 0; for (int i = 24000; i < 48000; ++i) pk = std::max(pk, (double)std::abs(l[i]));
    CHECK(20 * std::log10(pk / 0.447) > -0.2);
}

// the common oversampling setting (spec 共通機能: 1x / 2x / 4x, default 2x)
TEST_CASE("EQ05: the oversampling parameter is the last one, 1x / 2x / 4x, default 2x, and the Drive stage follows it") {
    const auto& s = eq05::specs();
    REQUIRE(s.back().steps == std::vector<double>{1, 2, 4});
    CHECK(std::string(s.back().id) == "eq05.os"); CHECK(s.back().def == 2.0); CHECK(static_cast<int>(s.size()) - 1 == eq05::Oversample);
    auto alias = [](int os) { eq05::Processor p; p.setParam(eq05::Drive, 10); p.setParam(eq05::Oversample, os); p.prepare(ost::kFs, 256); p.snapToTargets(); return ost::relDb(p, 15000, 3000, 0.3); };
    const double a1 = alias(1), a2 = alias(2), a4 = alias(4);
    INFO("15 kHz at Drive 10, alias at 3 kHz: 1x " << a1 << " dB, 2x " << a2 << " dB, 4x " << a4 << " dB");
    CHECK(a1 > -45.0); CHECK(a2 < -60.0); CHECK(ost::notWorse(a4, a2));
}
