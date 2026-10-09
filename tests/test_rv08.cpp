#include "doctest.h"
#include "rv08/rv08.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::rv08;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// a burst (noise or sine) of `ms` at `dbfs` starting at `at` seconds, in `total` seconds of silence
std::vector<float> burst(double dbfs, double ms, double at, double total, double f = 0, unsigned seed = 3) {
    std::vector<float> x(static_cast<size_t>(total * kFs), 0.0f);
    const auto nz = noise(dbfs, ms * 0.001, seed); const auto sn = sine(dbfs, ms * 0.001, f > 0 ? f : 1000);
    for (size_t i = 0; i < nz.size(); ++i) x[static_cast<size_t>(at * kFs) + i] += f > 0 ? sn[i] : nz[i];
    return x;
}
double winDb(const std::vector<float>& y, double a, double b) { return rmsDb(y, static_cast<size_t>(a * kFs), static_cast<size_t>(b * kFs)); }
}

TEST_CASE("RV08 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"rv08.size", "rv08.gatetime", "rv08.threshold", "rv08.shape", "rv08.tone", "rv08.mix", "rv08.evo.on", "rv08.unit"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Size].min == 0); CHECK(s[Size].max == 10); CHECK(s[Size].def == 5);
    CHECK(s[GateTime].min == 50); CHECK(s[GateTime].max == 800); CHECK(s[GateTime].def == 250); CHECK(s[GateTime].curve == Curve::Log);
    CHECK(s[Threshold].min == 0); CHECK(s[Threshold].max == 10); CHECK(s[Threshold].def == 5);
    CHECK(s[Shape].def == 0); CHECK(s[Tone].def == 50); CHECK(s[Mix].def == 40);
    CHECK(s[Snare].def == 0);
    NEAR(thresholdDbfs(0), -60.0, 1e-9); NEAR(thresholdDbfs(5), -30.0, 1e-9); NEAR(thresholdDbfs(10), 0.0, 1e-9);
    NEAR(gateGain(0.0, 0.0), 1.0, 1e-9); NEAR(gateGain(0.9, 0.0), 1.0, 1e-9); NEAR(gateGain(0.0, 1.0), 0.0, 1e-9); NEAR(gateGain(1.0, 1.0), 1.0, 1e-9); NEAR(gateGain(0.5, 0.5), 0.75, 1e-9);
}
TEST_CASE("RV08 silence is silence; no delay is reported") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("RV08 the gate cuts the tail at Gate time") {
    for (double t : {100.0, 400.0}) {
        auto p = make({{GateTime, t}, {Size, 8}});
        const auto y = run(p, burst(-10, 20, 0.05, 1.5));
        const double open = winDb(y, 0.07 + 0.2 * t * 0.001, 0.07 + 0.5 * t * 0.001);   // inside the open time
        const double shut = winDb(y, 0.05 + t * 0.001 + 0.05, 1.4);                         // well after it
        CHECK(open > -60.0);
        CHECK(shut < open - 60.0);
    }
}
TEST_CASE("RV08 Threshold decides whether a hit opens the gate") {
    const auto x = burst(-40, 20, 0.05, 1.0);   // -40 dBFS rms burst, peaks about -30
    auto lo = make({{Threshold, 2}}); auto hi = make({{Threshold, 8}});
    const auto yl = run(lo, x), yh = run(hi, x);
    CHECK(winDb(yl, 0.1, 0.2) > -80.0);
    CHECK(winDb(yh, 0.1, 0.2) < -150.0);
}
TEST_CASE("RV08 Shape: Flat keeps the level, Reverse ramps up") {
    const double T = 0.5;
    auto f = make({{GateTime, 500}, {Shape, 0}, {Size, 10}, {Threshold, 4}}); auto r = make({{GateTime, 500}, {Shape, 100}, {Size, 10}, {Threshold, 4}});
    const auto x = burst(-10, 20, 0.05, 1.2);
    const auto yf = run(f, x), yr = run(r, x);
    const double early = 0.07 + 0.1 * T, late = 0.07 + 0.8 * T;
    // the reverse ramp (window means x = 0.15 .. 0.85: about +15 dB) against the same reverb with the flat gate: the rise is the ramp's, not the tail's
    const double rise = (winDb(yr, late, late + 0.05) - winDb(yr, early, early + 0.05)) - (winDb(yf, late, late + 0.05) - winDb(yf, early, early + 0.05));
    CHECK(rise > 12.0); CHECK(rise < 20.0);
    CHECK(winDb(yr, 0.07, 0.07 + 0.05 * T) < winDb(yf, 0.07, 0.07 + 0.05 * T) - 10.0);
}
TEST_CASE("RV08 a new hit restarts the time") {
    auto p = make({{GateTime, 200}, {Size, 8}}); auto q = make({{GateTime, 200}, {Size, 8}});
    auto one = burst(-10, 20, 0.05, 1.0); auto two = one;
    const auto b2 = burst(-10, 20, 0.2, 1.0, 0, 9); for (size_t i = 0; i < two.size(); ++i) two[i] += b2[i];   // second hit 150 ms after the first: inside the first window
    const auto y1 = run(p, one), y2 = run(q, two);
    CHECK(winDb(y1, 0.30, 0.34) < -100.0);    // the first alone closed at about 0.25 s
    CHECK(winDb(y2, 0.30, 0.34) > -60.0);     // the second keeps it open till about 0.4 s
}
TEST_CASE("RV08 Snare key: only the snare's bands open the gate") {
    const auto lowTone = burst(-20, 40, 0.05, 0.6, 1000.0), snareTone = burst(-20, 40, 0.05, 0.6, 3000.0), kick = burst(-20, 40, 0.05, 0.6, 60.0);
    auto run1 = [](bool k, const std::vector<float>& x) { auto p = make({{Snare, k ? 1.0 : 0.0}}); return winDb(run(p, x), 0.1, 0.3); };
    CHECK(run1(false, lowTone) > -80.0); CHECK(run1(false, kick) > -80.0);                 // Off: anything above threshold opens it
    CHECK(run1(true, snareTone) > -80.0);                                                  // On: 3 kHz opens
    CHECK(run1(true, kick) < -150.0); CHECK(run1(true, lowTone) < -150.0);                 // 60 Hz and 1 kHz do not
}
TEST_CASE("RV08 Tone tilts the spectrum, Size lengthens the tail") {
    auto d = make({{Tone, 0}, {GateTime, 800}, {Size, 8}}); auto b = make({{Tone, 100}, {GateTime, 800}, {Size, 8}});
    const auto x = burst(-10, 20, 0.05, 1.0);
    const auto yd = run(d, x), yb = run(b, x);
    const double rd = binDb(yd, 6000, 4800, 36000) - binDb(yd, 200, 4800, 36000), rb = binDb(yb, 6000, 4800, 36000) - binDb(yb, 200, 4800, 36000);
    CHECK(rb - rd > 6.0);
    auto s = make({{Size, 1}, {GateTime, 800}}); auto l = make({{Size, 9}, {GateTime, 800}});
    const auto ys = run(s, x), yl = run(l, x);
    CHECK(winDb(yl, 0.5, 0.7) > winDb(ys, 0.5, 0.7) + 6.0);
}
TEST_CASE("RV08 output is finite and bounded for loud input at the extremes") {
    for (double t : {50.0, 800.0}) for (double sh : {0.0, 100.0}) {
        auto p = make({{GateTime, t}, {Shape, sh}, {Size, 10}, {Threshold, 0}});
        const auto y = run(p, noise(0, 1.0, 5));
        for (float v : y) { CHECK(std::isfinite(v)); CHECK(std::abs(v) < 20.0f); }
    }
}
