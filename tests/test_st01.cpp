#include "doctest.h"
#include "st01/st01.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::st01;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
void go(Processor& p, std::vector<float>& l, std::vector<float>& r) { for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } }
// L = R (mid only) or L = -R (side only) tone
std::pair<std::vector<float>, std::vector<float>> tone(double f, bool side, double db = -12, double sec = 1.0) { auto l = sine(db, sec, f); auto r = l; if (side) for (auto& v : r) v = -v; return {l, r}; }
}

TEST_CASE("ST01 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"st01.low", "st01.lomid", "st01.himid", "st01.high", "st01.xover1", "st01.xover2", "st01.xover3", "st01.monocheck"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    for (int i = Low; i <= High; ++i) { CHECK(s[static_cast<size_t>(i)].min == 0); CHECK(s[static_cast<size_t>(i)].max == 200); CHECK(s[static_cast<size_t>(i)].def == 100); }
    for (int i = Xover1; i <= Xover3; ++i) { CHECK(s[static_cast<size_t>(i)].min == 20); CHECK(s[static_cast<size_t>(i)].max == 20000); CHECK(s[static_cast<size_t>(i)].curve == Curve::Log); }
    CHECK(s[Xover1].def == 200); CHECK(s[Xover2].def == 2000); CHECK(s[Xover3].def == 8000);
    CHECK(s[MonoCheck].def == 0); CHECK(!s[MonoCheck].automatable);   // monitoring: not for Auto
}
TEST_CASE("ST01 no delay is reported; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> l(48000, 0.0f), r = l; go(p, l, r); for (size_t i = 0; i < l.size(); ++i) { CHECK(l[i] == 0.0f); CHECK(r[i] == 0.0f); }
}
TEST_CASE("ST01 at 100 % everywhere the magnitude is untouched, mid and side alike") {
    for (double f : {40.0, 150.0, 600.0, 3000.0, 12000.0}) for (bool side : {false, true}) {
        auto p = make(); auto t = tone(f, side); const double in = rmsDb(t.first, 24000, 48000); go(p, t.first, t.second);
        NEAR(rmsDb(t.first, 24000, 48000), in, 0.1);
        // L and R stay each other's mirror image (the same phase in both channels)
        for (size_t i = 30000; i < 30100; ++i) NEAR(t.first[i] * (side ? -1.0 : 1.0), t.second[i], 1e-4);
    }
}
TEST_CASE("ST01 each band's width acts on its own band only") {
    // 40 Hz is in Low, 600 Hz in Lo mid, 4 kHz in Hi mid, 14 kHz in High
    const double fs4[4] = {40.0, 600.0, 4000.0, 14000.0};
    for (int band = 0; band < 4; ++band) {
        for (double w : {50.0, 200.0}) {
            auto p = make({{Low + band, w}}); auto t = tone(fs4[band], true); const double in = rmsDb(t.first, 24000, 48000); go(p, t.first, t.second);
            NEAR(rmsDb(t.first, 24000, 48000) - in, 20 * std::log10(w * 0.01), 1.0);
            // the other bands are not touched
            for (int other = 0; other < 4; ++other) if (other != band) {
                auto q = make({{Low + band, w}}); auto u = tone(fs4[other], true); const double in2 = rmsDb(u.first, 24000, 48000); go(q, u.first, u.second);
                NEAR(rmsDb(u.first, 24000, 48000), in2, 1.2);
            }
        }
        // width 0: the side is gone (the bands are phase-shifted against one another by the crossovers, so not to the last decibel: at least 15 dB)
        auto z = make({{Low + band, 0}}); auto t = tone(fs4[band], true); go(z, t.first, t.second); CHECK(rmsDb(t.first, 24000, 48000) < -12 - 15.0);
    }
    // mid is never touched, whatever the width
    for (int band = 0; band < 4; ++band) { auto p = make({{Low + band, 0}}); auto t = tone(fs4[band], false); const double in = rmsDb(t.first, 24000, 48000); go(p, t.first, t.second); NEAR(rmsDb(t.first, 24000, 48000), in, 0.2); }
}
TEST_CASE("ST01 Crossovers move the bands, and keep an octave apart") {
    // Low width 0: a side tone at 300 Hz is cut with Crossover 1 at 1 kHz, untouched with it at 100 Hz
    auto cut = [&](double x1) { auto p = make({{Low, 0}, {Xover1, x1}}); auto t = tone(300, true); const double in = rmsDb(t.first, 24000, 48000); go(p, t.first, t.second); return rmsDb(t.first, 24000, 48000) - in; };
    CHECK(cut(1000) < -30.0); CHECK(cut(100) > -3.0 - 3.0);
    // crossing values are pushed back (no throw, finite)
    auto p = make({{Xover1, 5000}, {Xover2, 1000}, {Xover3, 100}}); auto t = tone(1000, true); go(p, t.first, t.second); for (float v : t.first) CHECK(std::isfinite(v));
}
TEST_CASE("ST01 Mono check plays (L + R) / 2 on both sides") {
    auto p = make({{MonoCheck, 1}, {High, 200}}); auto l = noise(-18, 1.0, 3), r = noise(-18, 1.0, 4);
    auto q = make({}); auto l2 = l, r2 = r; go(q, l2, r2);   // the reference: untouched
    go(p, l, r);
    for (size_t i = 24000; i < 24200; ++i) { NEAR(l[i], r[i], 1e-6); }
    // with all widths 100 the mono sum is the input's mono sum
    auto p2 = make({{MonoCheck, 1}}); auto a = noise(-18, 1.0, 3), b = noise(-18, 1.0, 4); auto a0 = a, b0 = b; go(p2, a, b);
    std::vector<float> m(a0.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (a0[i] + b0[i]);
    NEAR(rmsDb(a, 24000, 48000), rmsDb(m, 24000, 48000), 0.5);   // the same energy (the crossovers only turn the phase)
    (void)l2; (void)r2;
}
TEST_CASE("ST01 correlation meters and the over-wide mark") {
    auto fill = [&](Processor& p, bool side) { auto t = tone(600, side, -12, 2.0); go(p, t.first, t.second); };
    { auto p = make(); fill(p, false); NEAR(p.correlation(1), 1.0, 0.01); CHECK(!p.overWide(1)); }
    { auto p = make(); fill(p, true); NEAR(p.correlation(1), -1.0, 0.01); CHECK(p.overWide(1)); CHECK(!p.overWide(3)); }
    { auto p = make(); auto l = noise(-18, 3.0, 3), r = noise(-18, 3.0, 4); go(p, l, r); CHECK(std::abs(p.correlation(1)) < 0.15); }
    // a band pushed wide: mostly mid plus side x 2 is still positive, but a side-heavy signal x 2 stays at -1
    { auto p = make({{LoMid, 200}}); auto l = noise(-18, 3.0, 3), r = l; for (size_t i = 0; i < r.size(); ++i) { const float m = 0.3f * l[i], s = 0.1f * l[i]; l[i] = m + s; r[i] = m - s; } go(p, l, r); CHECK(p.correlation(1) > 0.5); }
    { auto p = make({{LoMid, 200}}); auto l = noise(-18, 3.0, 3), r = l; for (size_t i = 0; i < r.size(); ++i) { const float m = 0.1f * l[i], s = 0.15f * l[i]; l[i] = m + s; r[i] = m - s; } go(p, l, r); CHECK(p.overWide(1)); }
    { auto p = make(); std::vector<float> z(48000, 0.0f), z2 = z; go(p, z, z2); CHECK(!p.overWide(0)); NEAR(p.correlation(0), 1.0, 1e-9); }
}
TEST_CASE("ST01 loud noise stays finite; a mono track passes untouched") {
    auto p = make({{Low, 200}, {LoMid, 200}, {HiMid, 200}, {High, 200}}); auto l = noise(0, 2.0, 5), r = noise(0, 2.0, 6); go(p, l, r);
    for (size_t i = 0; i < l.size(); ++i) { CHECK(std::isfinite(l[i])); CHECK(std::isfinite(r[i])); CHECK(std::abs(l[i]) < 10.0f); }
    auto q = make({{Low, 0}}); auto m = noise(-12, 0.5, 7), m0 = m; float* c[1] = {m.data()}; q.process(c, 1, static_cast<int>(m.size())); for (size_t i = 0; i < m.size(); ++i) CHECK(m[i] == m0[i]);
}
