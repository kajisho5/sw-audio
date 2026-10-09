#include "doctest.h"
#include "dy01/dy01.hpp"
#include "os_helpers.hpp"
#include "sw/text.hpp"
#include <cmath>
#include <complex>
#include <random>
#include <vector>
using namespace sw;
using namespace sw::dy01;
namespace {
const double kPi = 3.14159265358979323846, kFs = 48000.0;
Processor make(std::vector<std::pair<int, double>> set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> run(Processor& p, std::vector<float> l) {
    std::vector<float> r = l;
    for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); }
    return l;
}
std::vector<float> sine(double peakDbfs, int n, double f = 1000) { const double a = std::pow(10.0, peakDbfs / 20); std::vector<float> x(static_cast<size_t>(n)); for (int i = 0; i < n; ++i) x[static_cast<size_t>(i)] = static_cast<float>(a * std::sin(2 * kPi * f * i / kFs)); return x; }
double peakDb(const std::vector<float>& y, size_t a, size_t b) { double pk = 0; for (size_t i = a; i < b; ++i) pk = std::max(pk, static_cast<double>(std::abs(y[i]))); return 20 * std::log10(std::max(pk, 1e-9)); }
double bin(const std::vector<float>& y, double f, size_t a, size_t b) {
    std::complex<double> acc; double w = 0;
    for (size_t i = a; i < b; ++i) { const double h = 0.5 - 0.5 * std::cos(2 * kPi * (i - a) / (b - a - 1)); acc += h * static_cast<double>(y[i]) * std::exp(std::complex<double>(0, -2 * kPi * f * i / kFs)); w += h; }
    return 2 * std::abs(acc) / w;
}
double harmDb(const std::vector<float>& y, double f, int k) { return 20 * std::log10(bin(y, f * k, y.size() / 2, y.size()) / bin(y, f, y.size() / 2, y.size()) + 1e-12); }
// samples until the gain reduction has moved 63 % of the way from `from` to `to` (dB), after the input changes
}

TEST_CASE("DY01 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"dy01.drive", "dy01.ratio", "dy01.speed", "dy01.bite", "dy01.color", "dy01.out", "dy01.mix", "dy01.schpf", "dy01.os", "dy01.unit"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Drive].min == 0); CHECK(s[Drive].max == 10); CHECK(s[Drive].def == 0);
    CHECK(s[Ratio].min == 2); CHECK(s[Ratio].max == 20); CHECK(s[Ratio].def == 4); CHECK(s[Ratio].curve == Curve::Log);
    CHECK(std::string(s[Ratio].maxLabel) == "Max"); CHECK(s[Ratio].maxLabelNorm == doctest::Approx(0.95));
    CHECK(s[Speed].def == 0.5); CHECK(std::string(s[Speed].minLabel) == "Slow"); CHECK(std::string(s[Speed].maxLabel) == "Fast");
    CHECK(s[Bite].min == 0); CHECK(s[Bite].max == 100); CHECK(s[Bite].def == 0);
    CHECK(s[Color].labels == std::vector<std::string>{"Clean", "Grit", "Crush"}); CHECK(s[Color].def == 1);
    CHECK(s[Output].min == -12); CHECK(s[Output].max == 24); CHECK(s[Output].def == 0);
    CHECK(s[Mix].def == 100);
    CHECK(s[SchPf].min == 20); CHECK(s[SchPf].max == 300); CHECK(std::string(s[SchPf].minLabel) == "Off");
    for (const auto& p : s) CHECK(p.automatable);
}
TEST_CASE("DY01 Speed: 0.5 is about 130 us / 240 ms, ends are 800 us / 1100 ms and 20 us / 50 ms") {
    CHECK(attackMs(0.0) == doctest::Approx(0.8)); CHECK(attackMs(1.0) == doctest::Approx(0.02));
    CHECK(releaseMs(0.0) == doctest::Approx(1100.0)); CHECK(releaseMs(1.0) == doctest::Approx(50.0));
    CHECK(attackMs(0.5) == doctest::Approx(0.13).epsilon(0.05)); CHECK(releaseMs(0.5) == doctest::Approx(240.0).epsilon(0.05));
}
TEST_CASE("DY01 static curve: fixed -6 dBFS threshold, 4:1 (Clean)") {
    auto p = make({{Color, 0}, {Ratio, 4}, {Speed, 1}});
    // peak +6 dBFS is 12 dB over the threshold -> 3 dB over after compression
    const auto y = run(p, sine(6.0, 48000));
    CHECK(std::abs((peakDb(y, 24000, 48000)) - (-6.0 + 3.0)) < 1.0);
    auto q = make({{Color, 0}, {Ratio, 4}});
    CHECK(std::abs((peakDb(run(q, sine(-30.0, 24000)), 12000, 24000)) - (-30.0)) < 0.3);  // below the threshold: untouched
}
TEST_CASE("DY01 Drive 10 pushes +36 dB into the threshold") {
    auto p = make({{Color, 0}, {Drive, 5}});
    CHECK(std::abs((peakDb(run(p, sine(-40.0, 24000)), 12000, 24000)) - (-40.0 + 18.0)) < 1.0);
}
TEST_CASE("DY01 Ratio Max (right 5 %): hard knee, no more than 1 dB over after +12 dB") {
    auto p = make({{Color, 0}, {Ratio, 20}, {Speed, 1}});
    CHECK(p.gainReductionDb() == 0.0);
    CHECK(peakDb(run(p, sine(6.0, 48000)), 24000, 48000) < -6.0 + 1.0);
}
TEST_CASE("DY01 Speed links attack and release") {
    auto grAfter = [](double speed, double loudMs) {
        auto p = make({{Color, 0}, {Ratio, 8}, {Speed, speed}});
        auto x = sine(6.0, static_cast<int>(kFs * loudMs / 1000.0)); run(p, x);
        return p.gainReductionDb();
    };
    CHECK(grAfter(1.0, 0.3) < grAfter(0.0, 0.3) - 3.0);  // 0.3 ms in: the 20 us attack has closed down, the 800 us one is a third of the way
    auto recovery = [](double speed) {
        auto p = make({{Color, 0}, {Ratio, 8}, {Speed, speed}});
        run(p, sine(6.0, 48000));
        const double g0 = p.gainReductionDb();
        auto q = sine(-60.0, 96000); int n = 0;
        for (size_t off = 0; off < q.size(); off += 64) { std::vector<float> a(q.begin() + off, q.begin() + off + 64), b = a; float* c[2] = {a.data(), b.data()}; p.process(c, 2, 64); n += 64; if (p.gainReductionDb() > g0 * 0.368) break; }
        return 1000.0 * n / kFs;
    };
    CHECK(recovery(0.5) == doctest::Approx(releaseMs(0.5)).epsilon(0.25));
    CHECK(recovery(1.0) < recovery(0.0) / 10.0);
}
TEST_CASE("DY01 Bite lets the head of a hit through without changing the steady level") {
    auto hit = [](double bite) {
        auto p = make({{Color, 0}, {Ratio, 8}, {Speed, 0.5}, {Bite, bite}});
        std::vector<float> x(48000 * 2, 0.0f);
        for (int i = 0; i < 48000; ++i) x[static_cast<size_t>(i)] = static_cast<float>(0.05 * std::sin(2 * kPi * 200 * i / kFs));   // bed
        for (int i = 0; i < 24000; ++i) x[static_cast<size_t>(48000 + i)] = static_cast<float>(2.0 * std::exp(-i / 4000.0) * std::sin(2 * kPi * 200 * i / kFs));
        const auto y = run(p, x);
        return std::make_pair(peakDb(y, 48000, 48000 + 480), peakDb(y, 48000 + 12000, 48000 + 16000));
    };
    const auto off = hit(0), on = hit(100);
    CHECK(on.first > off.first + 1.5);              // transient head
    CHECK(std::abs((on.second) - (off.second)) < 1.0);  // tail unchanged
}
TEST_CASE("DY01 Color: Clean = a little even order, Grit = odd + even, Crush grows with gain reduction") {
    auto h = [](int color, double inDb, int k, double ratio = 4) { auto p = make({{Color, static_cast<double>(color)}, {Ratio, ratio}, {Speed, 0.5}}); return harmDb(run(p, sine(inDb, 96000, 1000)), 1000, k); };
    const double c2 = h(0, -3, 2), c3 = h(0, -3, 3);
    CHECK(c2 > -60.0); CHECK(c2 < -25.0); CHECK(c2 > c3);                       // Clean: even first, small
    const double g2 = h(1, -3, 2), g3 = h(1, -3, 3);
    CHECK(g2 > -50.0); CHECK(g3 > -50.0);                                       // Grit: both
    CHECK(h(1, 6, 3) > h(1, -12, 3) + 10.0);                                    // level dependent
    const double crushDeep = std::max(h(2, 12, 3), h(2, 12, 2)), crushLight = std::max(h(2, -4, 3), h(2, -4, 2));
    CHECK(crushDeep > crushLight + 8.0);                                        // grows with gain reduction
    CHECK(crushDeep > std::max(h(0, 12, 3), h(0, 12, 2)) + 6.0);
}
TEST_CASE("DY01 SC HPF keeps low frequencies out of the detector") {
    auto gr = [](double hpf) { auto p = make({{Color, 0}, {Ratio, 8}, {SchPf, hpf}}); run(p, sine(0.0, 48000, 40)); return p.gainReductionDb(); };
    CHECK(gr(20) < -3.0);                // Off (min): compresses
    CHECK(gr(300) > gr(20) + 5.0);       // 300 Hz high-pass: 40 Hz barely reaches the detector
}
TEST_CASE("DY01 stays finite and silent for silence and extreme input") {
    auto p = make({{Drive, 10}, {Color, 2}, {Ratio, 20}, {Bite, 100}});
    for (float v : run(p, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f);
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : run(p, x)) REQUIRE(std::isfinite(v));
    CHECK(p.latencySamples() == 0);
}

// the common oversampling setting (spec 共通機能: 1x / 2x / 4x, default 2x; the spec recommends 4x for Crush)
TEST_CASE("DY01: the oversampling parameter is the last one, 1x / 2x / 4x, default 2x, and the colour stage follows it (Crush runs at 4x unless the setting is 1x)") {
    const auto& s = specs();
    CHECK(std::string(s[Oversample].id) == "dy01.os"); CHECK(s[Oversample].steps == std::vector<double>{1, 2, 4}); CHECK(s[Oversample].def == 2.0); CHECK(Oversample == kNumParams - 2);
    auto alias = [](int color, int os) { auto p = make({{Drive, 4}, {Color, double(color)}, {Mix, 100}, {Oversample, double(os)}}); return ost::relDb(p, 15000, 3000, 0.3); };
    const double c1 = alias(0, 1), c2 = alias(0, 2), c4 = alias(0, 4);
    INFO("Clean, 15 kHz, alias at 3 kHz: 1x " << c1 << " dB, 2x " << c2 << " dB, 4x " << c4 << " dB");
    CHECK(c1 > -50.0); CHECK(c2 < c1 - 15.0); CHECK(ost::notWorse(c4, c2));
    const double k1 = alias(2, 1), k2 = alias(2, 2), k4 = alias(2, 4);
    INFO("Crush, 15 kHz, alias at 3 kHz: 1x " << k1 << " dB, 2x " << k2 << " dB, 4x " << k4 << " dB");
    CHECK(k1 > -50.0); CHECK(k2 < k1 - 15.0); CHECK((std::abs(k2 - k4) < 3.0 || (k2 < -100.0 && k4 < -100.0)));   // the 2x and 4x settings are both 4x for Crush
}
