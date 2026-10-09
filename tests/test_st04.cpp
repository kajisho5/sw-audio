#include "doctest.h"
#include "st04/st04.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::st04;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
void go(Processor& p, std::vector<float>& l, std::vector<float>& r) { for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } }
std::pair<std::vector<float>, std::vector<float>> tone(double f, bool side, double db = -12, double sec = 1.0) { auto l = sine(db, sec, f); auto r = l; if (side) for (auto& v : r) v = -v; return {l, r}; }
double level(Processor& p, double f, bool side) { auto t = tone(f, side); const double in = rmsDb(t.first, 24000, 48000); go(p, t.first, t.second); return rmsDb(t.first, 24000, 48000) - in; }
size_t peakAt(const std::vector<float>& y, size_t a, size_t b) { size_t k = a; for (size_t i = a; i < std::min(b, y.size()); ++i) if (std::abs(y[i]) > std::abs(y[k])) k = i; return k; }
}

TEST_CASE("ST04 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"st04.center", "st04.haas", "st04.side", "st04.lowcenter", "st04.balance", "st04.link", "st04.evo.on", "st04.unit"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Center].def == 50); CHECK(std::string(s[Center].minLabel) == "Wide"); CHECK(std::string(s[Center].maxLabel) == "Focus");
    CHECK(s[Haas].min == 0); CHECK(s[Haas].max == 40); CHECK(s[Haas].curve == Curve::Skew); CHECK(s[Haas].skew == 2); CHECK(s[Haas].def == 0);
    NEAR(s[Haas].toValue(0.5), 10.0, 1e-9);   // k = 2: x^2 x 40
    CHECK(s[Side].labels == std::vector<std::string>{"L", "R"}); CHECK(s[Side].def == 0);
    CHECK(s[LowCenter].min == 0); CHECK(s[LowCenter].max == 10); CHECK(s[LowCenter].def == 0);
    CHECK(s[Balance].min == -100); CHECK(s[Balance].max == 100); CHECK(s[Balance].def == 0); CHECK(s[Link].def == 1); CHECK(s[MonoSafe].def == 0);
}
TEST_CASE("ST04 no delay is reported; defaults leave the sound alone; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    for (double f : {80.0, 1000.0}) for (bool side : {false, true}) { auto p = make(); NEAR(level(p, f, side), 0.0, 0.01); }
    auto p = make(); std::vector<float> l(48000, 0.0f), r = l; go(p, l, r); for (size_t i = 0; i < l.size(); ++i) { CHECK(l[i] == 0.0f); CHECK(r[i] == 0.0f); }
}
TEST_CASE("ST04 Center: Wide +6 dB .. Focus mono on the side only") {
    NEAR(sideGain(0), std::pow(10.0, 6.0 / 20.0), 1e-9); NEAR(sideGain(25), std::pow(10.0, 3.0 / 20.0), 1e-9); NEAR(sideGain(50), 1.0, 1e-12);
    NEAR(sideGain(75), 0.5, 1e-12); NEAR(sideGain(100), 0.0, 1e-12); CHECK(sideGain(99) > 0.0);
    for (double c : {0.0, 25.0, 50.0, 75.0, 100.0}) {
        auto p = make({{Center, c}}); NEAR(level(p, 1000, true), 20 * std::log10(std::max(sideGain(c), 1e-9)), c == 100.0 ? 1e9 : 0.05);
        auto q = make({{Center, c}}); NEAR(level(q, 1000, false), 0.0, 0.01);   // the mid never moves
    }
    auto p = make({{Center, 100}}); auto t = tone(1000, true); go(p, t.first, t.second); for (size_t i = 24000; i < 24100; ++i) NEAR(t.first[i], t.second[i], 1e-6);   // Focus: L = R
}
TEST_CASE("ST04 Haas delays the chosen side") {
    for (double ms : {5.0, 10.0, 25.0}) for (int side : {0, 1}) {
        auto p = make({{Haas, ms}, {Side, double(side)}, {Link, 0}});
        std::vector<float> l(48000, 0.0f), r = l; l[100] = 1.0f; r[100] = 1.0f; go(p, l, r);
        const size_t d = static_cast<size_t>(std::lround(ms * 0.001 * kFs));
        const auto& del = side == 0 ? l : r; const auto& other = side == 0 ? r : l;
        CHECK(peakAt(del, 0, del.size()) == 100 + d); CHECK(peakAt(other, 0, other.size()) == 100);
        NEAR(del[100 + d], 1.0, 0.01); NEAR(other[100], 1.0, 1e-6);
    }
}
TEST_CASE("ST04 Link raises the delayed side by 0.35 dB per ms (at most 6 dB)") {
    for (double ms : {4.0, 10.0, 30.0}) {
        auto p = make({{Haas, ms}, {Side, 1}, {Link, 1}});
        std::vector<float> l(48000, 0.0f), r = l; l[100] = 1.0f; r[100] = 1.0f; go(p, l, r);
        const size_t d = static_cast<size_t>(std::lround(ms * 0.001 * kFs));
        NEAR(20 * std::log10(r[100 + d]), std::min(6.0, 0.35 * ms), 0.1);
        NEAR(l[100], 1.0, 1e-6);
        NEAR(p.delayedSideGainDb(), std::min(6.0, 0.35 * ms), 1e-9);
    }
    auto off = make({{Haas, 10}, {Side, 1}, {Link, 0}}); NEAR(off.delayedSideGainDb(), 0.0, 1e-9);
    NEAR(make({}).delayedSideGainDb(), 0.0, 1e-12);   // no Haas, nothing delayed
}
TEST_CASE("ST04 Low center makes the lows mono") {
    NEAR(lowCenterHz(0), 0.0, 1e-12); NEAR(lowCenterHz(10), 300.0, 1e-9); NEAR(lowCenterHz(1), 20.0 * std::pow(15.0, 0.1), 1e-9);
    for (double v : {2.0, 5.0, 10.0}) {
        const double f = lowCenterHz(v);
        { auto p = make({{LowCenter, v}}); CHECK(level(p, f * 0.25, true) < -20.0); }
        { auto p = make({{LowCenter, v}}); NEAR(level(p, f * 4.0, true), 0.0, 0.3); }
        { auto p = make({{LowCenter, v}}); NEAR(level(p, f * 0.25, false), 0.0, 0.01); }   // the mid is not touched
        { auto p = make({{LowCenter, v}}); NEAR(level(p, f, true), -3.0, 0.4); }          // the corner
    }
    auto p = make({{LowCenter, 0}}); NEAR(level(p, 25, true), 0.0, 0.01);   // Off
}
TEST_CASE("ST04 Balance: the louder side stays, the other goes down linearly") {
    auto p = make({{Balance, 50}}); std::vector<float> l(48000, 0.5f), r = l; go(p, l, r); NEAR(l[40000], 0.25, 1e-3); NEAR(r[40000], 0.5, 1e-3);
    auto q = make({{Balance, -100}}); std::vector<float> a(48000, 0.5f), b = a; go(q, a, b); NEAR(a[40000], 0.5, 1e-3); NEAR(b[40000], 0.0, 1e-3);
    auto z = make({{Balance, 0}}); std::vector<float> c(48000, 0.5f), d = c; go(z, c, d); NEAR(c[40000], 0.5, 1e-6); NEAR(d[40000], 0.5, 1e-6);
}
TEST_CASE("ST04 Mono safe keeps the comb of the mono sum shallower than 10 dB") {
    // equal levels and 5 ms: the mono sum has a first notch at 100 Hz: deep
    auto depth = [&](Set set) {
        auto p = make(set); auto a = sine(-12, 3.0, 100), b = a; go(p, a, b);
        auto q = make(set); auto c = sine(-12, 3.0, 200), d = c; go(q, c, d);   // 200 Hz: a peak (delay = a whole period)
        std::vector<float> m1(a.size()), m2(c.size()); for (size_t i = 0; i < a.size(); ++i) { m1[i] = 0.5f * (a[i] + b[i]); m2[i] = 0.5f * (c[i] + d[i]); }
        return rmsDb(m1, 96000, 144000) - rmsDb(m2, 96000, 144000);
    };
    Set off = {{Haas, 5.0}, {Side, 1}, {Link, 0}, {MonoSafe, 0}}, on = {{Haas, 5.0}, {Side, 1}, {Link, 0}, {MonoSafe, 1}};
    CHECK(depth(off) < -25.0);
    NEAR(depth(on), -10.0, 1.5);
    NEAR(make(on).monoCombDepthDb(), -10.0, 0.1); CHECK(make(off).monoCombDepthDb() < -50.0);
    // the delayed side's level was lowered to r = 0.52 (-5.7 dB), and its highs went down
    NEAR(make(on).delayedSideGainDb(), 20 * std::log10(0.52), 0.01);
    auto hi = [&](Set set) { auto p = make(set); auto a = sine(-12, 1.0, 12000), b = a; go(p, a, b); return rmsDb(b, 24000, 48000); };
    CHECK(hi(on) < hi(off) - 10.0);   // the right (delayed) channel at 12 kHz: lower than without Mono safe (level and low-pass)
    // with a quiet delayed side already, nothing is done
    Set quiet = {{Haas, 5.0}, {Side, 1}, {Link, 0}, {MonoSafe, 1}, {Balance, -50}};
    NEAR(make(quiet).delayedSideGainDb(), 20 * std::log10(0.5), 0.01);
}
TEST_CASE("ST04 a mono track passes; loud noise stays finite") {
    auto p = make({{Haas, 20}, {Center, 20}}); auto m = noise(-12, 0.5, 7), m0 = m; float* c[1] = {m.data()}; p.process(c, 1, static_cast<int>(m.size())); for (size_t i = 0; i < m.size(); ++i) CHECK(m[i] == m0[i]);
    auto q = make({{Center, 0}, {Haas, 40}, {LowCenter, 10}, {Balance, -30}, {MonoSafe, 1}}); auto l = noise(0, 2.0, 5), r = noise(0, 2.0, 6); go(q, l, r); for (size_t i = 0; i < l.size(); ++i) { CHECK(std::isfinite(l[i])); CHECK(std::isfinite(r[i])); CHECK(std::abs(l[i]) < 20.0f); }
}
