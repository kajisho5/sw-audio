#include "doctest.h"
#include "dy10/dy10.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::dy10;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> two(double f1, double db1, double f2, double db2, double seconds = 2.0) { auto a = sine(db1, seconds, f1), b = sine(db2, seconds, f2); for (size_t i = 0; i < a.size(); ++i) a[i] += b[i]; return a; }
}

TEST_CASE("DY10 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* kn[] = {"thresh", "ratio", "attack", "release", "range", "gain", "solo", "bypass"};
    for (int n = 0; n < 4; ++n) for (int k = 0; k < 8; ++k) CHECK(std::string(s[static_cast<size_t>(band(n, k))].id) == "dy10.b" + std::to_string(n + 1) + "." + kn[k]);
    CHECK(std::string(s[X1].id) == "dy10.x1.freq"); CHECK(std::string(s[X3].id) == "dy10.x3.freq"); CHECK(std::string(s[Output].id) == "dy10.out");
    for (int n = 0; n < 4; ++n) {
        const auto& t = s[static_cast<size_t>(band(n, BThresh))]; CHECK(t.min == -60); CHECK(t.max == 0); CHECK(t.def == 0);
        const auto& r = s[static_cast<size_t>(band(n, BRatio))]; CHECK(r.min == 1); CHECK(r.max == 20); CHECK(r.def == 2); CHECK(r.curve == Curve::Log);
        const auto& a = s[static_cast<size_t>(band(n, BAttack))]; CHECK(a.min == 0.1); CHECK(a.max == 200); CHECK(a.def == 15); CHECK(a.curve == Curve::Skew); CHECK(a.skew == 3);
        const auto& l = s[static_cast<size_t>(band(n, BRelease))]; CHECK(l.min == 5); CHECK(l.max == 3000); CHECK(l.def == 150); CHECK(l.skew == 3);
        const auto& g = s[static_cast<size_t>(band(n, BRange))]; CHECK(g.min == -24); CHECK(g.max == 0); CHECK(g.def == -12);
        const auto& m = s[static_cast<size_t>(band(n, BGain))]; CHECK(m.min == -12); CHECK(m.max == 12); CHECK(m.def == 0);
        CHECK_FALSE(s[static_cast<size_t>(band(n, BSolo))].automatable); CHECK(s[static_cast<size_t>(band(n, BBypass))].automatable);
    }
    CHECK(s[X1].def == 240); CHECK(s[X2].def == 2000); CHECK(s[X3].def == 8000); CHECK(s[X1].min == 20); CHECK(s[X1].max == 20000);
    CHECK(s[Output].min == -24); CHECK(s[Output].max == 24); CHECK(s[Output].def == 0);
}
TEST_CASE("DY10 the four bands add back flat") {
    for (double f : {30.0, 100.0, 240.0, 1000.0, 2000.0, 5000.0, 8000.0, 16000.0}) { auto p = make(); NEAR(rmsDb(run(p, sine(-30, 1, f))), -30.0, 0.15); }
}
TEST_CASE("DY10 each band compresses on its own") {
    auto p = make({{band(0, BThresh), -30}, {band(0, BRatio), 10}});   // band 1 only
    const auto y = run(p, two(100, -12, 5000, -30));
    const auto lowIn = binDb(two(100, -12, 5000, -30), 100, 48000, 96000);
    CHECK(binDb(y, 100, 48000, 96000) < lowIn - 5.0);                       // the 100 Hz tone goes down
    NEAR(binDb(y, 5000, 48000, 96000), binDb(two(100, -12, 5000, -30), 5000, 48000, 96000), 0.3);   // 5 kHz untouched
}
TEST_CASE("DY10 Range caps the gain reduction; Bypass skips the band; Gain adds") {
    auto level = [](Set s, double f = 100) { auto p = make(s); return rmsDb(run(p, sine(-10, 3, f))); };
    const double ref = level({});
    const double deep = level({{band(0, BThresh), -50}, {band(0, BRatio), 20}, {band(0, BRange), -6}});
    CHECK(deep < ref - 4.0); CHECK(deep > ref - 7.0);
    NEAR(level({{band(0, BThresh), -50}, {band(0, BRatio), 20}, {band(0, BBypass), 1}}), ref, 0.3);
    NEAR(level({{band(1, BGain), 6}}, 1000) - level({}, 1000), 6.0, 1.0);
    NEAR(level({{band(1, BGain), 6}, {band(1, BBypass), 1}}, 1000) - level({}, 1000), 0.0, 0.3);
}
TEST_CASE("DY10 Solo plays only the soloed band") {
    auto p = make({{band(2, BSolo), 1}});
    NEAR(rmsDb(run(p, sine(-20, 1, 3500))), -21.1, 0.4);   // band 3 (2-8 kHz) at 3.5 kHz: LR4 edges -1.1 dB (measured)
    auto q = make({{band(2, BSolo), 1}});
    CHECK(rmsDb(run(q, sine(-20, 1, 100))) < -60.0);
}
TEST_CASE("DY10 crossovers keep an octave apart (the mover is pushed back)") {
    Processor p; p.prepare(kFs, 256);
    p.setParam(X1, 1000); p.setParam(X2, 1500); p.setParam(X3, 3000);
    CHECK(p.crossoverHz(0) == doctest::Approx(1000)); CHECK(p.crossoverHz(1) == doctest::Approx(2000)); CHECK(p.crossoverHz(2) == doctest::Approx(4000));
    p.setParam(X1, 20000); p.setParam(X2, 20000); p.setParam(X3, 20000);
    CHECK(p.crossoverHz(2) <= 20000.0 + 1e-9); CHECK(p.crossoverHz(1) <= 10000.0 + 1e-9); CHECK(p.crossoverHz(0) <= 5000.0 + 1e-9);
}
TEST_CASE("DY10 Attack time: 0.1 ms vs 200 ms") {
    auto gr = [](double ms) { auto p = make({{band(1, BThresh), -40}, {band(1, BRatio), 10}, {band(1, BAttack), ms}}); run(p, sine(-10, 0.02, 1000)); return p.gainReductionDb(1); };
    CHECK(gr(0.1) < gr(200) - 3.0);
}
TEST_CASE("DY10 silence stays silent, extreme input finite, latency 0") {
    Set s; for (int n = 0; n < 4; ++n) { s.push_back({band(n, BThresh), -60}); s.push_back({band(n, BRatio), 20}); }
    auto p = make(s);
    for (float v : run(p, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f);
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : run(p, x)) REQUIRE(std::isfinite(v));
    CHECK(p.latencySamples() == 0);
}
