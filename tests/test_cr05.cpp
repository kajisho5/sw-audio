#include "doctest.h"
#include "cr05/cr05.hpp"
#include "voice.hpp"
using namespace sw;
using namespace sw::cr05;
using namespace tu;
namespace {
constexpr size_t kT0 = 48128;   // the first block boundary (256) at or after 1 s: where Trigger takes effect
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.setTempo(120.0); return p; }
// a steady tone; Trigger On at `on` s, Off at `off` s
std::vector<float> go(Processor& p, double seconds, double on, double off = 1e9, double hz = 1000.0, double toBar = -1.0) {
    std::vector<float> l = sine(-12, seconds, hz), r = l; bool a = false, b = false;
    for (size_t o = 0; o < l.size(); o += 256) {
        if (!a && o >= on * kFs) { if (toBar >= 0) p.setTransport(true, toBar); p.setParam(Trigger, 1); a = true; }
        if (!b && o >= off * kFs) { p.setParam(Trigger, 0); b = true; }
        const int n = static_cast<int>(std::min<size_t>(256, l.size() - o)); float* c[2] = {l.data() + o, r.data() + o}; p.process(c, 2, n); }
    return l; }
double zeroCrossHz(const std::vector<float>& y, size_t a, size_t b) { int c = 0; for (size_t i = a + 1; i < b; ++i) if ((y[i - 1] < 0) != (y[i] < 0)) ++c; return 0.5 * c * kFs / static_cast<double>(b - a); }
}

TEST_CASE("CR05 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"cr05.action", "cr05.stop", "cr05.start", "cr05.curve", "cr05.filter", "cr05.trigger"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Action].labels == std::vector<std::string>{"Stop", "Start", "Spin back"}); CHECK(s[Action].def == 0);
    CHECK(s[StopTime].labels == std::vector<std::string>{"1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars"}); CHECK(s[StopTime].def == 3); CHECK(s[StartTime].def == 1);
    CHECK(s[Curve_].labels == std::vector<std::string>{"Lin", "Exp", "Log"}); CHECK(s[Curve_].def == 1);
    CHECK(s[Filter].def == 1); CHECK(s[Trigger].def == 0); CHECK(s[Trigger].automatable);
    NEAR(barFraction(0), 1.0 / 16, 1e-12); NEAR(barFraction(5), 2.0, 1e-12);
}
TEST_CASE("CR05 no delay; silence is silence; with Trigger Off the signal passes untouched") {
    Processor q; CHECK(q.latencySamples() == 0);
    { auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f); }
    const auto x = noise(-18, 2.0, 3); auto p = make(); const auto y = run(p, x); for (size_t i = 0; i < x.size(); i += 13) CHECK(y[i] == x[i]);
}
TEST_CASE("CR05 Stop: the pitch falls with the speed, then silence; Trigger Off brings the live input back") {
    auto p = make({{StopTime, 3}, {Curve_, 0}, {Filter, 0}});   // 1/2 bar = 1 s at 120 bpm, Lin
    const auto y = go(p, 5.0, 1.0, 3.5);
    NEAR(zeroCrossHz(y, kT0 - 4800, kT0), 1000.0, 20.0);                      // before: the tone
    const double f1 = zeroCrossHz(y, kT0 + 12000, kT0 + 18000), f2 = zeroCrossHz(y, kT0 + 30000, kT0 + 36000);   // 0.25 .. 0.375 s and 0.625 .. 0.75 s into the stop
    NEAR(f1, 1000.0 * (1.0 - 0.31), 80.0); NEAR(f2, 1000.0 * (1.0 - 0.69), 80.0);   // the speed 1 - u
    CHECK(rmsDb(y, 2 * kT0 + 4800, 3 * 48000) < -80.0);                          // held: silent
    CHECK(rmsDb(y, 4 * 48000, 5 * 48000) > -14.0);                                  // released: live again
}
TEST_CASE("CR05 Curve shapes the fall") {
    auto at = [&](double curve) { auto p = make({{StopTime, 3}, {Curve_, curve}, {Filter, 0}}); const auto y = go(p, 3.0, 1.0); return zeroCrossHz(y, kT0 + 12000, kT0 + 18000); };
    const double lin = at(0), exp = at(1), lg = at(2);
    CHECK(exp > lin + 100.0); CHECK(lg < lin - 100.0);
}
TEST_CASE("CR05 Filter darkens the tape as it slows") {
    auto hf = [&](double f) { auto p = make({{StopTime, 3}, {Curve_, 0}, {Filter, f}}); const auto y = go(p, 3.0, 1.0, 1e9, 15000.0); return rmsDb(y, kT0 + 36000, kT0 + 42000); };
    CHECK(hf(1) < hf(0) - 4.0);
}
TEST_CASE("CR05 Start: from standstill up to the live input; Spin back runs backwards") {
    auto p = make({{Action, Start}, {StartTime, 3}, {Curve_, 0}, {Filter, 0}});   // 1 s
    const auto y = go(p, 4.0, 1.0);
    CHECK(rmsDb(y, kT0, kT0 + 1200) < -30.0);                                   // starts from standstill
    NEAR(zeroCrossHz(y, kT0 + 36000, kT0 + 42000), 1000.0 * (0.75 + 0.07), 120.0);
    NEAR(zeroCrossHz(y, 3 * 48000, 4 * 48000), 1000.0, 20.0); CHECK(rmsDb(y, 3 * 48000, 4 * 48000) > -14.0);   // then the live tone
    auto q = make({{Action, SpinBack}, {StopTime, 3}, {Curve_, 0}, {Filter, 0}}); const auto z = go(q, 4.0, 1.0);
    CHECK(rmsDb(z, kT0 + 36000, kT0 + 44000) > -30.0);                           // it is playing (backwards) near the end
    for (float v : z) CHECK(std::isfinite(v));
}
TEST_CASE("CR05 with the host transport a Stop ends on the bar line") {
    // 120 bpm: a bar is 2 s; the trigger comes with 3 beats (1.5 s) to the next bar; Stop 1/4 bar = 0.5 s starts 1 s later and ends on the bar line
    auto p = make({{StopTime, 2}, {Curve_, 0}, {Filter, 0}}); const auto y = go(p, 6.0, 1.0, 1e9, 1000.0, 3.0);
    double lvlBefore = rmsDb(y, kT0 + 24000, kT0 + 36000); CHECK(lvlBefore > -14.0);                           // 0.5 .. 0.75 s after the trigger: still full speed (the stop starts at 1.0 s)
    size_t silent = 0; for (size_t i = kT0 + 48000; i + 2400 < y.size(); i += 240) { if (rmsDb(y, i, i + 2400) < -60.0) { silent = i; break; } }
    NEAR(static_cast<double>(silent - kT0) / kFs, 1.5, 0.12);                                                        // silent from the bar line (1.5 s after the trigger)
}
TEST_CASE("CR05 loud input stays finite; stereo channels are separate") {
    auto p = make({{Filter, 1}}); p.setParam(Trigger, 1); auto x = noise(6, 2.0, 9); for (auto& v : x) v *= 8.0f; for (float v : run(p, x)) CHECK(std::isfinite(v));
    auto q = make(); const auto a = noise(-18, 1.0, 1), b = noise(-18, 1.0, 2); const auto r = run2(q, a, b);
    for (size_t i = 0; i < a.size(); i += 53) { CHECK(r.first[i] == a[i]); CHECK(r.second[i] == b[i]); }
}
