#include "doctest.h"
#include "st03/st03.hpp"
#include "tu.hpp"
#include <complex>
using namespace sw;
using namespace sw::st03;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
void go(Processor& p, std::vector<float>& l, std::vector<float>& r, const std::vector<float>* ref = nullptr) {
    for (size_t off = 0; off < l.size(); off += 256) {
        const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off};
        if (ref) { const float* s[1] = {ref->data() + off}; p.processWithSidechain(c, 2, n, s, 1); } else p.process(c, 2, n);
    }
}
size_t peakAt(const std::vector<float>& y, size_t a, size_t b) { size_t k = a; for (size_t i = a; i < std::min(b, y.size()); ++i) if (std::abs(y[i]) > std::abs(y[k])) k = i; return k; }
std::vector<float> delayed(const std::vector<float>& x, size_t d) { std::vector<float> y(x.size(), 0.0f); for (size_t i = d; i < x.size(); ++i) y[i] = x[i - d]; return y; }
}

TEST_CASE("ST03 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"st03.delay", "st03.phase", "st03.polarity", "st03.mix"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Delay].min == 0); CHECK(s[Delay].max == 20); CHECK(s[Delay].curve == Curve::Skew); CHECK(s[Delay].skew == 2); CHECK(s[Delay].def == 0);
    NEAR(s[Delay].toValue(0.5), 5.0, 1e-9);
    CHECK(s[Phase].min == -180); CHECK(s[Phase].max == 180); CHECK(s[Phase].def == 0);
    CHECK(s[Polarity].labels == std::vector<std::string>{"Normal", "Invert"}); CHECK(s[Mix].def == 100);
}
TEST_CASE("ST03 no delay is reported; defaults pass the signal untouched; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); auto l = noise(-12, 0.5, 3), r = noise(-12, 0.5, 4), l0 = l, r0 = r; go(p, l, r);
    for (size_t i = 0; i < l.size(); ++i) { CHECK(l[i] == l0[i]); CHECK(r[i] == r0[i]); }
    auto z = make({{Phase, 70}, {Delay, 3}}); std::vector<float> a(48000, 0.0f), b = a; go(z, a, b); for (size_t i = 0; i < a.size(); ++i) { CHECK(a[i] == 0.0f); CHECK(b[i] == 0.0f); }
}
TEST_CASE("ST03 Delay: 0.01 ms steps, 4-point Hermite") {
    for (double ms : {0.5, 3.34, 10.0, 20.0}) {
        auto p = make({{Delay, ms}});
        std::vector<float> l(48000, 0.0f), r = l; l[100] = 1.0f; r[100] = 1.0f; go(p, l, r);
        const double want = 100 + std::round(ms * 100.0) / 100.0 * 0.048 * 1000.0 / 1.0 * 1.0;   // samples at 48 kHz
        CHECK(std::abs(double(peakAt(l, 0, l.size())) - want) <= 1.0);
        CHECK(std::abs(double(peakAt(r, 0, r.size())) - want) <= 1.0);
    }
    // 3.337 ms rounds to 3.34 ms: the same as 3.34
    auto a = make({{Delay, 3.337}}), b = make({{Delay, 3.34}}); std::vector<float> x(4800, 0.0f), y = x; x[10] = 1.0f; y[10] = 1.0f; auto x2 = x, y2 = y; go(a, x, x2); go(b, y, y2);
    for (size_t i = 0; i < x.size(); ++i) NEAR(x[i], y[i], 1e-5);
}
TEST_CASE("ST03 Polarity inverts") {
    auto p = make({{Polarity, 1}}); auto l = noise(-12, 0.5, 3), r = l, l0 = l; go(p, l, r); for (size_t i = 0; i < l.size(); ++i) CHECK(l[i] == -l0[i]);
}
TEST_CASE("ST03 Phase rotates every frequency by the same angle (relative to the pair's own response), 180 inverts") {
    // compare two rotations: the difference of the output phases is the difference of the angles
    auto phaseOf = [&](double f, double deg) {
        auto p = make({{Phase, deg}}); auto l = sine(-12, 2.0, f), r = l; go(p, l, r);
        std::complex<double> acc; for (size_t i = 48000; i < 96000; ++i) acc += double(l[i]) * std::exp(std::complex<double>(0, -2 * 3.14159265358979323846 * f * double(i) / kFs));
        return std::arg(acc) * 180.0 / 3.14159265358979323846;
    };
    for (double f : {120.0, 400.0, 1500.0, 6000.0}) {
        const double base = phaseOf(f, 10.0);
        for (double deg : {-150.0, -90.0, 45.0, 90.0, 170.0}) { double d = phaseOf(f, deg) - base - (deg - 10.0); while (d > 180) d -= 360; while (d < -180) d += 360; NEAR(d, 0.0, 2.5); }
    }
    // the magnitude stays (an all-pass pair)
    auto p = make({{Phase, 100}}); auto l = sine(-12, 2.0, 700), r = l; go(p, l, r); NEAR(rmsDb(l, 48000, 96000), rmsDb(sine(-12, 2.0, 700), 48000, 96000), 0.1);
    // +-180 inverts relative to +-0 through the same path
    auto a = make({{Phase, 180}}), b = make({{Phase, 0.1}}); auto x = sine(-12, 2.0, 900), x2 = x, y = x, y2 = x; go(a, x, x2); go(b, y, y2); for (size_t i = 60000; i < 60100; ++i) NEAR(x[i], -y[i], 0.01);
}
TEST_CASE("ST03 the reference goes in on the sidechain and is not heard") {
    auto p = make({{Delay, 2}}); auto q = make({{Delay, 2}});
    auto l = noise(-12, 0.5, 3), r = l, l2 = l, r2 = l; auto ref = noise(-6, 0.5, 9);
    go(p, l, r, &ref); go(q, l2, r2);
    for (size_t i = 0; i < l.size(); ++i) NEAR(l[i], l2[i], 1e-9);
}
TEST_CASE("ST03 Auto align: the lag, polarity and phase against a reference") {
    const double fs = kFs; const size_t N = static_cast<size_t>(4.0 * fs) + 256 * 4;
    auto x = noise(-18, N / fs, 11); x.resize(N);
    auto collect = [&](Processor& p, const std::vector<float>& ref) {
        p.startAutoAlign(); CHECK(p.alignState() == Collecting);
        auto l = x, r = x; go(p, l, r, &ref);
        return p.alignState();
    };
    auto written = [&](Processor& p, double out[3]) { int id; double v; int n = 0; while (p.takeParamWrite(id, v) == 7) { out[id == Delay ? 0 : id == Phase ? 1 : 2] = v; ++n; } return n; };
    // (a) a plain delayed copy: Delay found to 0.02 ms, Phase 0, Normal
    for (double ms : {1.25, 3.37, 9.0, 17.5}) {
        Processor p = make(); const size_t d = static_cast<size_t>(std::lround(ms * 0.001 * fs));
        CHECK(collect(p, delayed(x, d)) == Ready); REQUIRE(p.analyse()); CHECK(p.alignState() == Done);
        NEAR(p.foundDelayMs(), static_cast<double>(d) / fs * 1000.0, 0.011); NEAR(p.foundPhaseDeg(), 0.0, 1e-9); CHECK(!p.foundInvert()); CHECK(p.confidence() > 0.9);
        double w[3] = {-1, -1, -1}; CHECK(written(p, w) == 3); NEAR(w[0], p.foundDelayMs(), 1e-9); NEAR(w[1], 0.0, 1e-9); NEAR(w[2], 0.0, 1e-9);
    }
    // (b) an inverted copy: the polarity is flipped, Phase stays 0
    { Processor p = make(); auto ref = delayed(x, 240); for (auto& v : ref) v = -v;
      CHECK(collect(p, ref) == Ready); REQUIRE(p.analyse()); NEAR(p.foundDelayMs(), 5.0, 0.011); CHECK(p.foundInvert()); NEAR(p.foundPhaseDeg(), 0.0, 1e-9); }
    // (c) a reference that went through this rotator (Phase 70): the angle is found again
    { Processor mic = make({{Delay, 5.0}, {Phase, 70}}); auto l = x, r = x; go(mic, l, r); const auto& ref = l;
      Processor p = make(); CHECK(collect(p, ref) == Ready); REQUIRE(p.analyse());
      NEAR(p.foundDelayMs(), 5.0, 0.06); NEAR(p.foundPhaseDeg(), 70.0, 3.0); CHECK(!p.foundInvert()); }   // the pair's own group delay (about 2 samples) is inside this reference
    // (d) silence and unrelated signals fail and leave the parameters alone
    { Processor p = make({{Delay, 2.0}}); std::vector<float> z(N, 0.0f); CHECK(collect(p, z) == Ready); CHECK(!p.analyse()); CHECK(p.alignState() == Failed); double w[3]; CHECK(written(p, w) == 0); }
    { Processor p = make({{Delay, 2.0}}); auto other = noise(-18, N / fs, 99); other.resize(N); CHECK(collect(p, other) == Ready); CHECK(!p.analyse()); CHECK(p.confidence() < kMinConfidence); }
    // (e) a reference that is EARLIER than the track cannot be reached by delaying: no result
    { Processor p = make(); auto early = x; early.erase(early.begin(), early.begin() + 150); early.resize(N, 0.0f); CHECK(collect(p, early) == Ready); CHECK(!p.analyse()); }
    // analyse() before the collection has finished does nothing
    { Processor p = make(); CHECK(!p.analyse()); p.startAutoAlign(); CHECK(!p.analyse()); }
}
TEST_CASE("ST03 loud noise stays finite") {
    auto p = make({{Delay, 20}, {Phase, 135}, {Polarity, 1}}); auto l = noise(0, 2.0, 5), r = noise(0, 2.0, 6); go(p, l, r); for (size_t i = 0; i < l.size(); ++i) { CHECK(std::isfinite(l[i])); CHECK(std::abs(l[i]) < 20.0f); }
}
