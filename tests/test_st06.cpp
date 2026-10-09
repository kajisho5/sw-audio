#include "doctest.h"
#include "st06/st06.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::st06;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
void go(Processor& p, std::vector<float>& l, std::vector<float>& r) { for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } }
std::pair<std::vector<float>, std::vector<float>> tone(double f, bool side, double db = -12, double sec = 2.0) { auto l = sine(db, sec, f); auto r = l; if (side) for (auto& v : r) v = -v; return {l, r}; }
double level(Processor& p, double f, bool side) { auto t = tone(f, side); const double in = rmsDb(t.first, 48000, 96000); go(p, t.first, t.second); return rmsDb(t.first, 48000, 96000) - in; }
}

TEST_CASE("ST06 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"st06.frequency", "st06.slope", "st06.sideboost", "st06.output", "st06.listen", "st06.unit"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Frequency].min == 20); CHECK(s[Frequency].max == 300); CHECK(s[Frequency].def == 120); CHECK(s[Frequency].curve == Curve::Log);
    CHECK(s[Slope].labels == std::vector<std::string>{"6", "12", "24", "48"}); CHECK(s[Slope].def == 24);
    CHECK(s[SideBoost].min == -6); CHECK(s[SideBoost].max == 6); CHECK(s[Output].min == -10); CHECK(s[Output].max == 10);
    CHECK(s[Listen].def == 0); CHECK(!s[Listen].automatable);
}
TEST_CASE("ST06 no delay is reported; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> l(48000, 0.0f), r = l; go(p, l, r); for (size_t i = 0; i < l.size(); ++i) { CHECK(l[i] == 0.0f); CHECK(r[i] == 0.0f); }
}
TEST_CASE("ST06 the side is high-passed at Frequency with the chosen slope; the mid is not touched") {
    for (double slope : {6.0, 12.0, 24.0, 48.0}) {
        { auto p = make({{Slope, slope}}); NEAR(level(p, 5000, true), 0.0, 0.1); }
        { auto p = make({{Slope, slope}}); NEAR(level(p, 30, false), 0.0, 0.01); NEAR(level(p, 120, false), 0.0, 0.01); }   // the mid: never
        // the corner: -3 dB at Frequency for a Butterworth high-pass of any order
        { auto p = make({{Slope, slope}}); NEAR(level(p, 120, true), -3.0103, 0.3); }
        // one octave below: -slope dB (minus a little at 6 dB/oct)
        { auto p = make({{Slope, slope}}); const double oct = level(p, 60, true); const double want = 10 * std::log10(1.0 + std::pow(2.0, slope / 6.0 * 2.0)) * -1.0 + 0.0; NEAR(oct, want, 0.7); }
    }
}
TEST_CASE("ST06 Frequency moves the corner") {
    for (double f : {30.0, 120.0, 300.0}) { auto p = make({{Frequency, f}}); NEAR(level(p, f, true), -3.0103, 0.3); }
}
TEST_CASE("ST06 below Frequency the output is the mid alone (mono)") {
    auto p = make({{Frequency, 200}, {Slope, 48}}); auto l = sine(-12, 2.0, 40), r = l;   // 40 Hz with a side part: L 1.0, R 0.5 of the amplitude
    for (auto& v : r) v *= 0.5f;
    go(p, l, r);
    double d = 0, e = 0; for (size_t i = 48000; i < 96000; ++i) { d += (l[i] - r[i]) * (l[i] - r[i]); e += l[i] * l[i]; }
    CHECK(d / e < 1e-4);   // L and R equal: -40 dB
}
TEST_CASE("ST06 Side boost lifts the side above Frequency only; Output is a gain") {
    for (double db : {-6.0, 6.0}) { auto p = make({{SideBoost, db}}); NEAR(level(p, 4000, true), db, 0.7); auto r = make({{SideBoost, db}}); NEAR(level(r, 4000, false), 0.0, 0.01); }
    for (double db : {-10.0, 0.0, 10.0}) { auto p = make({{Output, db}}); NEAR(level(p, 1000, false), db, 0.05); auto q = make({{Output, db}}); NEAR(level(q, 4000, true), db, 0.1); }
}
TEST_CASE("ST06 Listen plays what the mono-ing removes: the side below Frequency") {
    auto low = [&](bool side, double f) { auto p = make({{Listen, 1}, {Slope, 24}}); auto t = tone(f, side); go(p, t.first, t.second); return rmsDb(t.first, 48000, 96000) - rmsDb(sine(-12, 2.0, f), 48000, 96000); };
    CHECK(low(true, 30) > -1.5);      // a low side tone is heard (almost fully)
    CHECK(low(true, 4000) < -40.0);   // a high one is not
    CHECK(low(false, 30) < -100.0);   // the mid is not (nothing to remove)
    // both channels carry the same signal
    auto p = make({{Listen, 1}}); auto t = tone(40, true); go(p, t.first, t.second); for (size_t i = 60000; i < 60100; ++i) NEAR(t.first[i], t.second[i], 1e-7);
}
TEST_CASE("ST06 a mono track passes; loud noise stays finite") {
    auto p = make({}); auto m = noise(-12, 0.5, 7), m0 = m; float* c[1] = {m.data()}; p.process(c, 1, static_cast<int>(m.size())); for (size_t i = 0; i < m.size(); ++i) CHECK(m[i] == m0[i]);
    for (double slope : {6.0, 48.0}) { auto q = make({{Slope, slope}, {Frequency, 300}, {SideBoost, 6}, {Output, 10}}); auto l = noise(0, 2.0, 5), r = noise(0, 2.0, 6); go(q, l, r); for (size_t i = 0; i < l.size(); ++i) { CHECK(std::isfinite(l[i])); CHECK(std::isfinite(r[i])); } }
}
