#include "doctest.h"
#include "cr02/cr02.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::cr02;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
// 120 bpm, the transport at the start of a bar; only the steps in `on` (0-based) are set
Processor make(Set set = {}, std::vector<int> on = {}, bool onlyThese = true) {
    Processor p; p.prepare(kFs, 256); p.setTempo(120.0);
    if (onlyThese) { p.clearPattern(); for (int s : on) p.setParam(Step01 + s, 1); }
    for (auto& s : set) p.setParam(s.first, s.second);
    p.setTransport(true, 4.0); return p;
}
std::vector<float> go(Processor& p, const std::vector<float>& x) { return run(p, x); }
constexpr size_t kStep = 6000;   // 1/16 at 120 bpm
double errDb(const std::vector<float>& y, size_t ya, const std::vector<float>& x, size_t xa, size_t n, int dir = 1, int stride = 1) {
    double s = 0, e = 0; for (size_t k = 0; k < n; ++k) { const double a = y[ya + k], b = x[static_cast<size_t>(static_cast<long>(xa) + dir * static_cast<long>(k) * stride)]; s += (a - b) * (a - b); e += b * b; } return 10 * std::log10(s / (e + 1e-30) + 1e-30); }
}

TEST_CASE("CR02 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(std::string(s[Grid].id) == "cr02.grid"); CHECK(s[Grid].labels == std::vector<std::string>{"1/8", "1/16", "1/32", "Triplet"}); CHECK(s[Grid].def == 1);
    CHECK(s[Gate].def == 60); CHECK(s[Repeat].labels == std::vector<std::string>{"1x", "2x", "4x", "8x", "16x"}); CHECK(s[Repeat].def == 4);
    CHECK(s[Pitch].min == -12); CHECK(s[Pitch].max == 12); CHECK(s[Pitch].def == 0);
    CHECK(s[Reverse].def == 0); CHECK(s[Filter].min == 200); CHECK(s[Filter].max == 20000); CHECK(s[Filter].def == 4000); CHECK(s[Filter].curve == Curve::Log); CHECK(s[Mix].def == 100);
    CHECK(std::string(s[Step01].id) == "cr02.step01"); CHECK(std::string(s[Step16].id) == "cr02.step16");
    for (int i = Step01; i <= Step16; ++i) CHECK_FALSE(s[static_cast<size_t>(i)].automatable);
    NEAR(gridBeats(0), 0.5, 1e-12); NEAR(gridBeats(1), 0.25, 1e-12); NEAR(gridBeats(2), 0.125, 1e-12); NEAR(gridBeats(3), 1.0 / 3.0, 1e-12);
}
TEST_CASE("CR02 no delay; silence is silence; with no step set the signal passes untouched") {
    Processor q; CHECK(q.latencySamples() == 0);
    { auto p = make({}, {}); std::vector<float> z(48000, 0.0f); for (float v : go(p, z)) CHECK(v == 0.0f); }
    const auto x = noise(-18, 3.0, 3); auto p = make({}, {}); const auto y = go(p, x);
    for (size_t i = 0; i < x.size(); i += 17) CHECK(y[i] == x[i]);
}
TEST_CASE("CR02 a step repeats the slice that has just gone by, Repeat times") {
    const auto x = noise(-18, 2.0, 3);
    auto p = make({{Gate, 100}, {Filter, 20000}, {Repeat, 4}}, {2}); const auto y = go(p, x);   // step 3 starts at 2 x 6000
    const size_t s = 2 * kStep, S = kStep / 4;
    for (size_t r = 0; r < 4; ++r) CHECK(errDb(y, s + r * S + 100, x, s - S + 100, S - 200) < -24.0);     // every repeat is the slice
    for (size_t i = 0; i < s; i += 97) CHECK(y[i] == x[i]);                                                    // before the step: dry
    for (size_t i = s + kStep + 10; i < s + kStep + 3000; i += 97) CHECK(y[i] == x[i]);                       // after: dry again
}
TEST_CASE("CR02 Gate is the open part of every repeat") {
    const auto x = noise(-18, 2.0, 3); const size_t s = 2 * kStep, S = kStep / 4;
    auto p = make({{Gate, 50}, {Filter, 20000}, {Repeat, 4}}, {2}); const auto y = go(p, x);
    for (size_t r = 0; r < 4; ++r) { CHECK(rmsDb(y, s + r * S + S / 2 + 100, s + r * S + S - 100) < -80.0); CHECK(rmsDb(y, s + r * S + 100, s + r * S + S / 2 - 100) > -30.0); }
}
TEST_CASE("CR02 Reverse and Pitch") {
    const auto x = noise(-18, 2.0, 3); const size_t s = 2 * kStep, S = kStep / 2;
    { auto p = make({{Gate, 100}, {Filter, 20000}, {Repeat, 2}, {Reverse, 1}}, {2}); const auto y = go(p, x);
      CHECK(errDb(y, s + 100, x, s - 1 - 100, S - 200, -1) < -24.0); }       // y[s + k] = x[s - 1 - k]
    { auto p = make({{Gate, 100}, {Filter, 20000}, {Repeat, 2}, {Pitch, 12}}, {2}); const auto y = go(p, x);
      CHECK(errDb(y, s + 100, x, s - S + 200, S / 2 - 300, 1, 2) < -20.0); }  // twice the speed: y[s + k] = x[s - S + 2 k]
}
TEST_CASE("CR02 Mix and Filter") {
    const auto x = noise(-18, 2.0, 3); const size_t s = 2 * kStep;
    { auto p = make({{Mix, 0}}, {2}); const auto y = go(p, x); for (size_t i = s; i < s + kStep; i += 7) CHECK(y[i] == x[i]); }
    auto hf = [&](double f) { auto p = make({{Gate, 100}, {Filter, f}, {Repeat, 1}}, {2}); const auto y = go(p, x); double e = 0; for (size_t i = s + 500; i < s + kStep - 500; ++i) { const double d = y[i] - (y[i - 1] ); e += d * d; } return 10 * std::log10(e + 1e-30); };
    CHECK(hf(500) < hf(20000) - 12.0);
}
TEST_CASE("CR02 Clear and Randomize (the UI buttons); Randomize puts steps by their place in the beat") {
    Processor p; p.prepare(kFs, 256); p.setTempo(120.0);
    p.clearPattern(); for (int i = 0; i < 16; ++i) CHECK_FALSE(p.stepOn(i));
    int onBeat = 0, offBeat = 0, eighth = 0; const int trials = 400;
    for (int t = 0; t < trials; ++t) { p.randomize();
        for (int i = 0; i < 16; ++i) if (p.stepOn(i)) { if (i % 4 == 0) ++onBeat; else if (i % 4 == 2) ++eighth; else ++offBeat; } }
    const double pOn = onBeat / (4.0 * trials), pEighth = eighth / (4.0 * trials), pOff = offBeat / (8.0 * trials);
    NEAR(pOn, 0.85, 0.06); NEAR(pEighth, 0.6, 0.06); NEAR(pOff, 0.35, 0.06);
}
TEST_CASE("CR02 Randomize leans toward the steps where the input had an onset") {
    auto density = [&](bool pulses) {
        Processor p; p.prepare(kFs, 256); p.setTempo(120.0); p.setTransport(true, 4.0);
        std::vector<float> x(static_cast<size_t>(3 * kFs), 0.0f);
        if (pulses) for (size_t b = 0; b < 3; ++b) for (size_t i = 0; i < 400; ++i) x[b * 4 * 24000 / 2 + 1 * kStep + 100 + i] = 0.5f * static_cast<float>(std::sin(i * 0.2));   // an onset at step 2 of every bar (bar = 48000 samples at 120 bpm)
        run(p, x);   // the bars go by
        int cnt = 0; for (int t = 0; t < 300; ++t) { p.randomize(); if (p.stepOn(1)) ++cnt; }
        return cnt / 300.0; };
    NEAR(density(false), 0.35, 0.08); CHECK(density(true) > 0.55);
}
TEST_CASE("CR02 loud input stays finite; stereo channels are separate") {
    auto p = make({{Repeat, 16}, {Pitch, 7}}, {0, 1, 2, 3, 4, 5}); auto x = noise(6, 2.0, 9); for (auto& v : x) v *= 8.0f; for (float v : go(p, x)) CHECK(std::isfinite(v));
    auto q = make({}, {}); const auto a = noise(-18, 1.0, 1), b = noise(-18, 1.0, 2); const auto r = run2(q, a, b);
    for (size_t i = 0; i < a.size(); i += 53) { CHECK(r.first[i] == a[i]); CHECK(r.second[i] == b[i]); }
}
