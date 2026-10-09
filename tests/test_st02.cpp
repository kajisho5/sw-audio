#include "doctest.h"
#include "st02/st02.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::st02;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
void go(Processor& p, std::vector<float>& l, std::vector<float>& r) { for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } }
std::pair<std::vector<float>, std::vector<float>> tone(double f, bool side, double db = -12, double sec = 1.0) { auto l = sine(db, sec, f); auto r = l; if (side) for (auto& v : r) v = -v; return {l, r}; }
double level(Processor& p, double f, bool side) { auto t = tone(f, side); const double in = rmsDb(t.first, 24000, 48000); go(p, t.first, t.second); return rmsDb(t.first, 24000, 48000) - in; }
}

TEST_CASE("ST02 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"st02.midlevel", "st02.sidelevel", "st02.sidehpf", "st02.sideair", "st02.midlow", "st02.encode", "st02.unit"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[MidLevel].min == -12); CHECK(s[MidLevel].max == 12); CHECK(s[SideLevel].min == -12); CHECK(s[SideLevel].max == 12);
    CHECK(s[SideHpf].min == 20); CHECK(s[SideHpf].max == 500); CHECK(s[SideHpf].curve == Curve::Log); CHECK(std::string(s[SideHpf].minLabel) == "Off");
    CHECK(s[SideAir].max == 10); CHECK(s[MidLow].min == -6); CHECK(s[MidLow].max == 6); CHECK(s[Encode].def == 0);
}
TEST_CASE("ST02 no delay is reported; defaults leave the sound alone; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    for (double f : {80.0, 1000.0, 12000.0}) for (bool side : {false, true}) { auto p = make(); NEAR(level(p, f, side), 0.0, 0.01); }
    auto p = make(); std::vector<float> l(48000, 0.0f), r = l; go(p, l, r); for (size_t i = 0; i < l.size(); ++i) { CHECK(l[i] == 0.0f); CHECK(r[i] == 0.0f); }
}
TEST_CASE("ST02 Mid level and Side level move only their own part") {
    for (double db : {-12.0, -6.0, 6.0, 12.0}) {
        { auto p = make({{MidLevel, db}}); NEAR(level(p, 1000, false), db, 0.05); }
        { auto p = make({{MidLevel, db}}); NEAR(level(p, 1000, true), 0.0, 0.05); }
        { auto p = make({{SideLevel, db}}); NEAR(level(p, 1000, true), db, 0.05); }
        { auto p = make({{SideLevel, db}}); NEAR(level(p, 1000, false), 0.0, 0.05); }
    }
    // a left-only signal: M = S = L / 2; Side level -inf-ish makes it mono
    auto p = make({{SideLevel, -12}}); std::vector<float> l = sine(-12, 1.0, 500), r(l.size(), 0.0f); go(p, l, r);
    CHECK(rmsDb(l, 24000, 48000) > rmsDb(r, 24000, 48000) + 0.0);
}
TEST_CASE("ST02 Side HPF cuts the low side only; Off at the minimum") {
    auto p = make({{SideHpf, 200}}); CHECK(level(p, 40, true) < -20.0);
    auto q = make({{SideHpf, 200}}); NEAR(level(q, 40, false), 0.0, 0.05);   // the mid is not touched
    auto r = make({{SideHpf, 200}}); NEAR(level(r, 4000, true), 0.0, 0.2);
    auto s = make({{SideHpf, 20}}); NEAR(level(s, 25, true), 0.0, 0.05);      // Off: 25 Hz passes
    auto h = make({{SideHpf, 100}}); NEAR(level(h, 100, true), -3.0, 0.3);    // the corner: -3 dB
}
TEST_CASE("ST02 Side air and Mid low are shelves on their own channel") {
    for (double air : {0.0, 5.0, 10.0}) { auto p = make({{SideAir, air}}); NEAR(level(p, 16000, true), air * 0.6 * 0.96, 0.8); NEAR(level(p, 200, true), 0.0, 0.1); auto q = make({{SideAir, air}}); NEAR(level(q, 16000, false), 0.0, 0.05); }
    for (double db : {-6.0, 6.0}) { auto p = make({{MidLow, db}}); NEAR(level(p, 30, false), db, 0.5); NEAR(level(p, 5000, false), 0.0, 0.1); auto q = make({{MidLow, db}}); NEAR(level(q, 30, true), 0.0, 0.05); }
}
TEST_CASE("ST02 Encode: M in the left channel, S in the right, and they stay that way") {
    auto p = make({{Encode, 1}, {MidLevel, 6}, {SideLevel, -6}});
    std::vector<float> m = sine(-12, 1.0, 1000), s = sine(-18, 1.0, 1000, kFs), m0 = m, s0 = s; go(p, m, s);
    NEAR(rmsDb(m, 24000, 48000) - rmsDb(m0, 24000, 48000), 6.0, 0.05); NEAR(rmsDb(s, 24000, 48000) - rmsDb(s0, 24000, 48000), -6.0, 0.05);
    // not encoded, the same settings act on L / R through the matrix: a mid-only signal gets +6 dB
    auto q = make({{MidLevel, 6}, {SideLevel, -6}}); NEAR(level(q, 1000, false), 6.0, 0.05);
}
TEST_CASE("ST02 a mono track passes; loud noise stays finite") {
    auto p = make({{MidLevel, 6}}); auto m = noise(-12, 0.5, 7), m0 = m; float* c[1] = {m.data()}; p.process(c, 1, static_cast<int>(m.size())); for (size_t i = 0; i < m.size(); ++i) CHECK(m[i] == m0[i]);
    auto q = make({{MidLevel, 12}, {SideLevel, 12}, {SideAir, 10}, {MidLow, 6}, {SideHpf, 500}}); auto l = noise(0, 2.0, 5), r = noise(0, 2.0, 6); go(q, l, r); for (size_t i = 0; i < l.size(); ++i) { CHECK(std::isfinite(l[i])); CHECK(std::isfinite(r[i])); }
}
