#include "doctest.h"
#include "lo03/lo03.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lo03;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> tone(double amp, double f, double sec) {
    std::vector<float> x(static_cast<size_t>(sec * kFs)); for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(amp * std::sin(2 * kPi * f * static_cast<double>(i) / kFs)); return x;
}
double at(const std::vector<float>& y, double f) { return binDb(y, f, y.size() / 2, y.size()); }
// decaying 'kick' bursts: f Hz, decay time constant tau, one hit every `every` seconds, total seconds
std::vector<float> hits(double amp, double f, double tau, double every, double sec, double first = 0.1) {
    std::vector<float> x(static_cast<size_t>(sec * kFs), 0.0f);
    for (double t0 = first; t0 < sec; t0 += every)
        for (size_t i = static_cast<size_t>(t0 * kFs); i < x.size(); ++i) { const double t = static_cast<double>(i) / kFs - t0; if (t > 6 * tau) break; x[i] += static_cast<float>(amp * std::exp(-t / tau) * std::sin(2 * kPi * f * t)); }
    return x;
}
std::vector<float> sum(std::vector<float> a, const std::vector<float>& b) { for (size_t i = 0; i < a.size() && i < b.size(); ++i) a[i] += b[i]; return a; }
// stereo run with an external key (same length as x)
std::vector<float> runKey(Processor& p, std::vector<float> l, const std::vector<float>& key) {
    std::vector<float> r = l;
    for (size_t off = 0; off < l.size(); off += 256) {
        const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off};
        const float* k[2] = {key.data() + off, key.data() + off}; p.processWithSidechain(c, 2, n, k, 2);
    }
    return l;
}
double wdb(const std::vector<float>& y, double a, double b) { return rmsDb(y, static_cast<size_t>(a * kFs), static_cast<size_t>(b * kFs)); }
Set kick(double tight = 100) { return {{Role, 0}, {Tight, tight}, {Focus, 55}, {MonoBelow, 20}}; }
}

TEST_CASE("LO03 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"lo03.role", "lo03.focus", "lo03.tight", "lo03.mudcut", "lo03.monobelow"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Role].labels == std::vector<std::string>{"Kick", "Bass", "Both"}); CHECK(s[Role].def == 2);
    CHECK(s[Focus].min == 30); CHECK(s[Focus].max == 120); CHECK(s[Focus].def == 55); CHECK(s[Focus].curve == Curve::Log);
    CHECK(s[Tight].max == 100); CHECK(s[Tight].def == 50);
    CHECK(s[MudCut].min == 150); CHECK(s[MudCut].max == 500); CHECK(s[MudCut].def == 250); CHECK(s[MudCut].curve == Curve::Log);
    CHECK(s[MonoBelow].min == 20); CHECK(s[MonoBelow].max == 300); CHECK(s[MonoBelow].def == 120); CHECK(std::string(s[MonoBelow].minLabel) == "Off");
}
TEST_CASE("LO03 no delay; Tight 0 and Mono below Off pass the signal untouched") {
    Processor p; CHECK(p.latencySamples() == 0);
    auto q = make({{Tight, 0}, {MonoBelow, 20}}); const auto x = noise(-20, 1.0); CHECK(run(q, x) == x);
}
TEST_CASE("LO03 Mud cut is a static bell: 6 dB x Tight at its own frequency") {
    for (auto [tight, db] : std::vector<std::pair<double, double>>{{100, -6.0}, {50, -3.0}, {0, 0.0}}) {
        Set s = kick(tight); s.push_back({MudCut, 250}); auto p = make(s); const auto x = tone(0.2, 250, 2.0), y = run(p, x);
        NEAR(at(y, 250) - at(x, 250), db, 0.5);
    }
    Set s = kick(100); s.push_back({MudCut, 400}); auto p = make(s); const auto x = tone(0.2, 400, 2.0), y = run(p, x);
    NEAR(at(y, 400) - at(x, 400), -6.0, 0.5);
    auto q = make(s); const auto h = tone(0.2, 4000, 1.0), yh = run(q, h); NEAR(at(yh, 4000) - at(h, 4000), 0.0, 0.2);
}
TEST_CASE("LO03 Mono below removes the sides under the corner and keeps the mids") {
    Set s = {{Role, 0}, {Tight, 0}, {MonoBelow, 120}};
    for (double f : {60.0, 1000.0}) {
        auto p = make(s); const auto l = tone(0.3, f, 1.5); std::vector<float> r(l.size()); for (size_t i = 0; i < l.size(); ++i) r[i] = -l[i];
        const auto o = run2(p, l, r);
        const double d = at(o.first, f) - at(l, f);
        if (f < 100) CHECK(d < -20.0); else NEAR(d, 0.0, 0.2);
    }
    auto q = make(s); const auto m = tone(0.3, 60, 1.5); const auto o = run2(q, m, m); NEAR(at(o.first, 60) - at(m, 60), 0.0, 0.2); CHECK(o.first == o.second);
}
TEST_CASE("LO03 Kick: the tail of the Focus band is tightened, the hit and a steady note are not") {
    const auto x = hits(0.5, 55, 0.2, 1.0, 4.0);
    auto off = make(kick(0)); auto on = make(kick(100));
    const auto y0 = run(off, x), y1 = run(on, x);
    CHECK(wdb(y1, 3.35, 3.9) < wdb(y0, 3.35, 3.9) - 3.0);   // tail 250..800 ms after the third hit... (hit at 3.1 s)
    const size_t a = static_cast<size_t>(3.1 * kFs);
    NEAR(peakDb(y1, a, a + 2400), peakDb(y0, a, a + 2400), 1.0);
    auto s0 = make(kick(0)); auto s1 = make(kick(100)); const auto st = tone(0.3, 55, 3.0);
    NEAR(rmsDb(run(s1, st)), rmsDb(run(s0, st)), 1.0);
}
TEST_CASE("LO03 Focus picks the band that is tightened") {
    const auto x = hits(0.5, 100, 0.2, 1.0, 4.0);
    Set a = kick(100); a.push_back({Focus, 100}); Set b = kick(100); b.push_back({Focus, 30});
    auto pa = make(a); auto pb = make(b); auto p0 = make(kick(0));
    const auto ya = run(pa, x), yb = run(pb, x), y0 = run(p0, x);
    CHECK(wdb(ya, 3.35, 3.9) < wdb(y0, 3.35, 3.9) - 3.0);
    CHECK(wdb(yb, 3.35, 3.9) > wdb(y0, 3.35, 3.9) - 2.5);   // a 30 Hz bell reaches a 100 Hz tail only through its skirt (-1.6 dB measured)
}
TEST_CASE("LO03 Bass is ducked by the key's kick onsets only while they last") {
    const auto bass = tone(0.3, 55, 4.0), key = hits(0.6, 60, 0.15, 1.0, 4.0, 0.5);
    Set s = {{Role, 1}, {Tight, 100}, {Focus, 55}, {MonoBelow, 20}}, z = s; z[1].second = 0;
    auto on = make(s); auto off = make(z);
    const auto y1 = runKey(on, bass, key), y0 = runKey(off, bass, key);
    CHECK(wdb(y1, 2.52, 2.58) < wdb(y0, 2.52, 2.58) - 3.0);        // just after the hit at 2.5 s
    NEAR(wdb(y1, 2.9, 3.0), wdb(y0, 2.9, 3.0), 0.5);               // well after it
    auto nokey = make(s); const auto yn = run(nokey, bass); NEAR(rmsDb(yn), rmsDb(bass), 0.4);   // no key: nothing to duck on
    Set k = s; k[0].second = 0; auto kk = make(k); const auto yk = runKey(kk, bass, key); NEAR(wdb(yk, 2.52, 2.58), wdb(bass, 2.52, 2.58), 0.5);   // Kick ignores the key
}
TEST_CASE("LO03 Both estimates the onsets in the track itself") {
    const auto bass = tone(0.2, 50, 4.0), kk = hits(0.6, 60, 0.12, 1.0, 4.0, 0.5), x = sum(bass, kk);
    Set s = {{Role, 2}, {Tight, 100}, {Focus, 55}, {MonoBelow, 20}}, z = s; z[1].second = 0;
    auto on = make(s); auto off = make(z);
    const auto y1 = run(on, x), y0 = run(off, x);
    CHECK(wdb(y1, 2.54, 2.62) < wdb(y0, 2.54, 2.62) - 2.0);
    auto q = make(s); const auto st = tone(0.2, 50, 3.0); NEAR(rmsDb(run(q, st)), rmsDb(st), 0.5);   // a steady note alone is left alone
}
TEST_CASE("LO03 silence and loud noise stay finite") {
    auto p = make({}); std::vector<float> z(24000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
    for (double fo : {30.0, 55.0, 120.0}) { auto q = make({{Focus, fo}, {Tight, 100}, {MudCut, 150}, {MonoBelow, 300}}); for (float v : run(q, noise(-6, 1.0))) CHECK(std::isfinite(v)); }
}
