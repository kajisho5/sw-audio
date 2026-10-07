#include "doctest.h"
#include "mt04/mt04.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::mt04;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); return p; }
void feed2(Processor& p, std::vector<float> l, std::vector<float> r) { for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } }
}

TEST_CASE("MT04 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(std::string(s[Persistence].id) == "mt04.persistence"); CHECK(s[Persistence].min == 0.1); CHECK(s[Persistence].max == 5); CHECK(s[Persistence].def == 1); CHECK(s[Persistence].curve == Curve::Log);
    CHECK(std::string(s[Zoom].id) == "mt04.zoom"); CHECK(s[Zoom].labels == std::vector<std::string>{"1x", "2x", "4x", "8x"}); CHECK(s[Zoom].def == 1);
}
TEST_CASE("MT04 the signal passes unchanged; no delay") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> l = noise(-18, 1.0, 3), r = noise(-18, 1.0, 4); const auto l0 = l, r0 = r; feed2(p, l, r);
    std::vector<float> a = l0, b = r0; for (size_t off = 0; off < a.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, a.size() - off)); float* c[2] = {a.data() + off, b.data() + off}; p.process(c, 2, n); }
    for (size_t i = 0; i < a.size(); ++i) { CHECK(a[i] == l0[i]); CHECK(b[i] == r0[i]); }
}
TEST_CASE("MT04 correlation: mono +1, opposite phase -1, independent about 0") {
    const auto n1 = noise(-18, 3.0, 3), n2 = noise(-18, 3.0, 4); std::vector<float> inv = n1; for (auto& v : inv) v = -v;
    { auto p = make(); feed2(p, n1, n1); NEAR(p.correlation(), 1.0, 1e-3); NEAR(p.summedLossDb(), 3.0, 0.05); }
    { auto p = make(); feed2(p, n1, inv); NEAR(p.correlation(), -1.0, 1e-3); CHECK(p.summedLossDb() < -40.0); }
    { auto p = make(); feed2(p, n1, n2); NEAR(p.correlation(), 0.0, 0.15); NEAR(p.summedLossDb(), 0.0, 0.5); }
}
TEST_CASE("MT04 the bands: a negative correlation in one band only is flagged after 0.7 s") {
    // 125 Hz band in opposite phase, a 4 kHz tone in phase
    const size_t n = static_cast<size_t>(3 * kFs); std::vector<float> l(n), r(n); const auto lo = sine(-18, 3.0, 125), hi = sine(-18, 3.0, 4000);
    for (size_t i = 0; i < n; ++i) { l[i] = lo[i] + hi[i]; r[i] = -lo[i] + hi[i]; }
    auto p = make(); p.setParam(Persistence, 1.0);
    feed2(p, std::vector<float>(l.begin(), l.begin() + 24000), std::vector<float>(r.begin(), r.begin() + 24000)); CHECK(p.warnings() == 0);   // 0.5 s: not yet
    feed2(p, std::vector<float>(l.begin() + 24000, l.end()), std::vector<float>(r.begin() + 24000, r.end()));
    CHECK((p.warnings() & (1 << 1)) != 0);   // the 125 Hz band
    CHECK((p.warnings() & (1 << 6)) == 0);   // the 4 kHz band is fine
    CHECK(p.bandCorrelation(1) < -0.5); CHECK(p.bandCorrelation(6) > 0.5);
    // it clears when the correlation is back
    feed2(p, lo, lo); CHECK((p.warnings() & (1 << 1)) == 0);
}
TEST_CASE("MT04 the scope: mono is a vertical line, Persistence keeps the points, Zoom scales") {
    auto p = make({{Persistence, 0.5}}); const auto x = noise(-12, 2.0, 3); feed2(p, x, x);
    CHECK(p.pointCount() == static_cast<int>(std::lround(0.5 * kFs / kPointStep)));
    for (int i = 0; i < p.pointCount(); i += 37) { double px, py; p.scopePoint(i, px, py); NEAR(px, 0.0, 1e-6); }
    auto q = make({{Persistence, 5}}); feed2(q, x, x); CHECK(q.pointCount() == 2 * static_cast<int>(kFs) / kPointStep);
    double x1, y1, x8, y8; q.scopePoint(100, x1, y1); q.setParam(Zoom, 8); q.scopePoint(100, x8, y8); NEAR(y8, 8.0 * y1, 1e-6);
    // a side-only signal is horizontal
    auto s = make(); std::vector<float> inv = x; for (auto& v : inv) v = -v; feed2(s, x, inv); double sx, sy; s.scopePoint(s.pointCount() - 1, sx, sy); NEAR(sy, 0.0, 1e-6);
}
TEST_CASE("MT04 silence and loud input are finite") {
    auto p = make(); feed2(p, std::vector<float>(48000, 0.0f), std::vector<float>(48000, 0.0f)); NEAR(p.correlation(), 0.0, 1e-9); CHECK(p.warnings() == 0);
    auto x = noise(6, 2.0, 9); for (auto& v : x) v *= 8.0f; feed2(p, x, x); CHECK(std::isfinite(p.correlation()));
}
