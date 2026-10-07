#include "doctest.h"
#include "lv26/lv26.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv26;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> neg(std::vector<float> x) { for (auto& v : x) v = -v; return x; }
}

TEST_CASE("LV26 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Width].min == 0); CHECK(s[Width].max == 200); CHECK(s[Width].def == 100); CHECK(s[LowMono].min == 20); CHECK(s[LowMono].max == 300); CHECK(s[LowMono].def == 120); CHECK(std::string(s[LowMono].minLabel) == "Off");
    CHECK(s[MonoCheck].def == 0); CHECK(s[MonoCheck].automatable == false); CHECK(s[AutoPhaseFix].def == 1);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV26 neutral settings pass the signal bit for bit") {
    auto p = make({{LowMono, 20}, {AutoPhaseFix, 0}}); const auto l = noise(-20, 1.0, 3), r = noise(-20, 1.0, 4); const auto o = run2(p, l, r);
    for (size_t i = 0; i < l.size(); ++i) { NEAR(o.first[i], l[i], 1e-7); NEAR(o.second[i], r[i], 1e-7); }
    auto a = make({{LowMono, 20}}); const auto o2 = run2(a, l, l); for (size_t i = 0; i < l.size(); ++i) { REQUIRE(o2.first[i] == l[i]); }   // in-phase: the correction is zero
}
TEST_CASE("LV26 Width scales the side; 0 is mono, 200 doubles it") {
    const auto l = noise(-20, 1.0, 3), r = noise(-20, 1.0, 4);
    auto z = make({{Width, 0}, {LowMono, 20}, {AutoPhaseFix, 0}}); const auto o = run2(z, l, r); for (size_t i = 0; i < l.size(); i += 13) { NEAR(o.first[i], 0.5f * (l[i] + r[i]), 1e-7); NEAR(o.second[i], o.first[i], 1e-9); }
    auto d = make({{Width, 200}, {LowMono, 20}, {AutoPhaseFix, 0}}); const auto q = run2(d, l, r); for (size_t i = 0; i < l.size(); i += 13) NEAR(q.first[i] - q.second[i], 2.0f * (l[i] - r[i]), 1e-6);
}
TEST_CASE("LV26 Low mono removes the side below the frequency only") {
    auto p = make({{LowMono, 200}, {AutoPhaseFix, 0}}); const auto lo = sine(-20, 1.0, 50), hi = sine(-20, 1.0, 2000);
    const auto a = run2(p, lo, neg(lo)); CHECK(rmsDb(a.first, 24000, 48000) < -20.0 - 20.0);
    auto q = make({{LowMono, 200}, {AutoPhaseFix, 0}}); const auto b = run2(q, hi, neg(hi)); CHECK(std::abs(rmsDb(b.first, 24000, 48000) - (-20.0)) < 0.5);
}
TEST_CASE("LV26 Mono check puts the mono sum on both channels") {
    auto p = make({{MonoCheck, 1}, {LowMono, 20}, {AutoPhaseFix, 0}}); const auto l = noise(-20, 0.5, 3), r = noise(-20, 0.5, 4); const auto o = run2(p, l, r);
    for (size_t i = 0; i < l.size(); i += 7) { NEAR(o.first[i], 0.5f * (l[i] + r[i]), 1e-7); REQUIRE(o.first[i] == o.second[i]); }
}
TEST_CASE("LV26 Auto phase fix lowers the side of a band that cancels in mono, and only that band") {
    const auto a = sine(-20, 3.0, 2000), b = sine(-20, 3.0, 300);   // 2 kHz out of phase, 300 Hz in phase
    std::vector<float> l(a.size()), r(a.size()); for (size_t i = 0; i < a.size(); ++i) { l[i] = a[i] + b[i]; r[i] = -a[i] + b[i]; }
    auto on = make({{LowMono, 20}}); const auto o = run2(on, l, r); auto off = make({{LowMono, 20}, {AutoPhaseFix, 0}}); const auto z = run2(off, l, r);
    const double sideOn = binDb(o.first, 2000, 96000, 144000), sideOff = binDb(z.first, 2000, 96000, 144000);
    CHECK(sideOff - sideOn > 8.0); CHECK(sideOff - sideOn < 13.0);
    CHECK(std::abs(binDb(o.first, 300, 96000, 144000) - binDb(z.first, 300, 96000, 144000)) < 0.3);   // the mono part is untouched
    CHECK(on.bandCorrelation(2) < -0.9); CHECK(on.bandReductionDb(2) < -10.0); CHECK(on.bandCorrelation(0) > 0.9); CHECK(std::abs(on.bandReductionDb(0)) < 0.1);
}
TEST_CASE("LV26 a normal stereo mix is left alone; mono input; odd blocks; before prepare") {
    auto p = make({{LowMono, 20}}); const auto l = noise(-20, 4.0, 3), r = noise(-20, 4.0, 4); const auto o = run2(p, l, r);
    CHECK(std::abs(rmsDb(o.first, 96000, 192000) - rmsDb(l, 96000, 192000)) < 0.6);   // uncorrelated channels: nothing to fix
    auto m = make(); std::vector<float> x = noise(-20, 1.0, 3), keep = x; for (size_t off = 0; off < x.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, x.size() - off)); float* c[1] = {x.data() + off}; m.process(c, 1, n); } CHECK(x == keep);
    auto q = make(); std::vector<float> a = noise(-20, 1.0, 5), b = noise(-20, 1.0, 6); for (size_t off = 0; off < a.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, a.size() - off)); float* c[2] = {a.data() + off, b.data() + off}; q.process(c, 2, n); }
    for (float v : a) REQUIRE(std::isfinite(v));
    Processor z; std::vector<float> u(256, 0.3f), v(256, 0.2f); float* c[2] = {u.data(), v.data()}; z.process(c, 2, 256); CHECK(u[0] == 0.3f);
}
