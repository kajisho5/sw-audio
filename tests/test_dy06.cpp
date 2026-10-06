#include "doctest.h"
#include "dy06/dy06.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::dy06;
using namespace tu;
namespace {
Processor make(std::vector<std::pair<int, double>> set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// steady GR (dB) for a tone whose RMS is `over` dB above the threshold
double grAt(double over, std::vector<std::pair<int, double>> set = {}) {
    auto p = make(set); const double thr = -3.0 * p.thresholdKnob();
    run(p, sine(thr + over - 3.0103 + 3.0103, 4.0)); return p.gainReductionDb();
}
}

TEST_CASE("DY06 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"dy06.input", "dy06.thresh", "dy06.time", "dy06.mu", "dy06.mix", "dy06.stereo", "dy06.evo.on"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Input].min == 0); CHECK(s[Input].max == 10); CHECK(s[Input].def == doctest::Approx(3.3));
    CHECK(s[Threshold].def == 0); CHECK(s[Threshold].max == 10);
    CHECK(s[Time].steps == std::vector<double>{1, 2, 3, 4, 5, 6}); CHECK(s[Time].def == 3);
    CHECK(s[Mu].def == 0.5); CHECK(std::string(s[Mu].minLabel) == "Soft"); CHECK(std::string(s[Mu].maxLabel) == "Hard");
    CHECK(s[Mix].def == 100);
    CHECK(s[Stereo].labels == std::vector<std::string>{"Link", "Dual"}); CHECK(s[Stereo].def == 0);
    CHECK(s[Density].def == 1); CHECK(s[Density].automatable);
    CHECK(inputDb(0) == doctest::Approx(-10.0)); CHECK(inputDb(3.3) == doctest::Approx(-0.1)); CHECK(inputDb(10) == doctest::Approx(20.0));
    CHECK(attackMs(1) == 2); CHECK(attackMs(2) == 2); CHECK(attackMs(3) == 4); CHECK(attackMs(4) == 8); CHECK(attackMs(5) == 4); CHECK(attackMs(6) == 2);
}
TEST_CASE("DY06 Input knob 3.3 is 0 dB; the stage is clean for small signals") {
    auto p = make({{Input, 3.3}});
    NEAR(rmsDb(run(p, sine(-30, 2))), -30.0, 0.3);
    auto q = make({{Input, 10}});
    NEAR(rmsDb(run(q, sine(-50, 2))), -30.0, 0.5);   // +20 dB
}
TEST_CASE("DY06 the ratio rises with depth (about 1.5:1 near the threshold, toward 6:1 deep); Mu sets how fast") {
    const std::vector<std::pair<int, double>> s = {{Input, 3.3}, {Threshold, 10}, {Time, 6}};  // threshold -30 dBFS (the tube stage saturates near +6 dBFS)
    const double g0 = grAt(0, s), g6 = grAt(6, s), g12 = grAt(12, s);
    const double ratioNear = 1.0 / (1.0 + (g6 - g0) / 6.0), ratioMid = 1.0 / (1.0 + (g12 - g6) / 6.0);   // incremental ratios
    CHECK(ratioNear < 2.3); CHECK(ratioMid > 3.2); CHECK(ratioMid > ratioNear * 1.5);
    auto hard = s; hard.push_back({Mu, 1}); auto soft = s; soft.push_back({Mu, 0});
    CHECK(grAt(12, hard) < grAt(12, soft) - 1.5);
}
TEST_CASE("DY06 Time sets the attack: 2 / 2 / 4 / 8 / 4 / 2 ms") {
    auto gr = [](int t, double ms) { auto p = make({{Threshold, 5}, {Time, static_cast<double>(t)}}); run(p, sine(-5, ms / 1000.0)); return p.gainReductionDb(); };
    CHECK(gr(4, 4.0) > gr(1, 4.0) + 1.0);   // 8 ms attack has not got as far after 4 ms
}
TEST_CASE("DY06 Time sets the release: 0.3 / 0.8 / 1.5 / 3 s fixed") {
    auto rel = [](int t) {
        auto p = make({{Threshold, 5}, {Time, static_cast<double>(t)}, {Density, 0}}); run(p, sine(-5, 6));
        const double g0 = p.gainReductionDb(); const auto q = sine(-70, 30.0);
        for (size_t off = 0; off + 480 <= q.size(); off += 480) { std::vector<float> a(q.begin() + off, q.begin() + off + 480), b = a; float* c[2] = {a.data(), b.data()}; p.process(c, 2, 480); if (p.gainReductionDb() > g0 * 0.368) return static_cast<double>(off + 480) / kFs; }
        return 99.0;
    };
    NEAR(rel(1), 0.3, 0.12); NEAR(rel(3), 1.5, 0.5); NEAR(rel(4), 3.0, 1.0);
    CHECK(rel(2) > rel(1)); CHECK(rel(4) > rel(3));
}
TEST_CASE("DY06 Auto release (Time 5, 6) has a fast and a slow stage") {
    auto p = make({{Threshold, 5}, {Time, 5}, {Density, 0}}); run(p, sine(-5, 8));
    const double g0 = p.gainReductionDb(); const auto q = sine(-70, 20.0); double t50 = 99, t90 = 99;
    for (size_t off = 0; off + 480 <= q.size(); off += 480) { std::vector<float> a(q.begin() + off, q.begin() + off + 480), b = a; float* c[2] = {a.data(), b.data()}; p.process(c, 2, 480); const double t = static_cast<double>(off + 480) / kFs; if (t50 > 90 && p.gainReductionDb() > g0 * 0.5) t50 = t; if (p.gainReductionDb() > g0 * 0.1) { t90 = t; break; } }
    CHECK(t50 < 1.0); CHECK(t90 > 2.0); CHECK(t90 < 12.0);
}
TEST_CASE("DY06 Density adapt: dense sound -> longer release (up to 2x), sparse -> shorter (down to 0.5x)") {
    auto factor = [](const std::vector<float>& x, int on) { auto p = make({{Density, static_cast<double>(on)}}); run(p, x); return p.releaseScale(); };
    const auto dense = sine(-12, 6);   // steady, low crest factor
    std::vector<float> sparse(static_cast<size_t>(6 * kFs), 0.0f); for (size_t i = 0; i < sparse.size(); i += 24000) for (size_t k = 0; k < 120; ++k) sparse[i + k] = 0.5f * static_cast<float>(std::sin(k * 0.6) * std::exp(-0.03 * k));
    CHECK(factor(dense, 1) > 1.5); CHECK(factor(sparse, 1) < 0.8);
    CHECK(factor(dense, 0) == 1.0); CHECK(factor(sparse, 0) == 1.0);
}
TEST_CASE("DY06 tube stage adds even-order harmonics, 2nd above 3rd") {
    auto p = make({{Threshold, 0}});
    const auto y = run(p, sine(-12, 2, 1000));
    const double h2 = harmDb(y, 1000, 2), h3 = harmDb(y, 1000, 3);
    CHECK(h2 > -60.0); CHECK(h2 > h3 + 3.0); CHECK(h2 < -25.0);
}
TEST_CASE("DY06 Link follows the louder channel, Dual compresses each side on its own") {
    auto quietSide = [](int stereo) {
        auto p = make({{Threshold, 6}, {Stereo, static_cast<double>(stereo)}});
        auto [l, r] = run2(p, sine(-8, 3), sine(-40, 3, 700)); return rmsDb(r) ;
    };
    CHECK(quietSide(0) < quietSide(1) - 3.0);   // Link pulls the quiet channel down too
    NEAR(quietSide(1), -40.0, 0.5);              // Dual leaves it alone
}
TEST_CASE("DY06 silence stays silent, extreme input finite, latency 0") {
    auto p = make({{Input, 10}, {Threshold, 10}});
    for (float v : run(p, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f);
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : run(p, x)) REQUIRE(std::isfinite(v));
    CHECK(p.latencySamples() == 0);
}
