#include "doctest.h"
#include "md02/md02.hpp"
#include "sw/notes.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::md02;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}, double bpm = 0.0) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); if (bpm > 0) p.setTempo(bpm); p.snapToTargets(); return p; }
std::vector<float> impulse(double sec) { std::vector<float> x(static_cast<size_t>(sec * kFs), 0.0f); x[0] = 1.0f; return x; }
size_t peakAt(const std::vector<float>& y, size_t a, size_t b) { size_t k = a; for (size_t i = a; i < std::min(b, y.size()); ++i) if (std::abs(y[i]) > std::abs(y[k])) k = i; return k; }
}

TEST_CASE("MD02 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"md02.rate", "md02.depth", "md02.feedback", "md02.manual", "md02.evo.on", "md02.sync", "md02.mix"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Rate].min == 0.01); CHECK(s[Rate].max == 10); CHECK(s[Rate].def == 0.2); CHECK(s[Rate].curve == Curve::Log);
    CHECK(s[Depth].def == 70); CHECK(s[Feedback].min == -100); CHECK(s[Feedback].max == 100); CHECK(s[Feedback].def == 60);
    CHECK(s[Manual].min == 0.1); CHECK(s[Manual].max == 10); CHECK(s[Manual].def == 3); CHECK(s[Manual].curve == Curve::Log);
    CHECK(s[ThroughZero].def == 1); CHECK(s[Sync].def == 0); CHECK(s[Mix].def == 50);
}
TEST_CASE("MD02 the reported latency is 480 samples with Through zero (10 ms at 48 kHz), 0 without") {
    { Processor p; CHECK(p.latencySamples() == 480); }
    { Processor p; p.setParam(ThroughZero, 0); CHECK(p.latencySamples() == 0); }
    { Processor p; p.setParam(ThroughZero, 1); p.prepare(96000.0, 256); CHECK(p.latencySamples() == 960); }
    // the value for the next prepare: switching the parameter after prepare changes the answer, the running core keeps its prepared latency
    Processor p; p.prepare(kFs, 256); p.setParam(ThroughZero, 0); CHECK(p.latencySamples() == 0);
}
TEST_CASE("MD02 silence is silence") {
    for (double tz : {0.0, 1.0}) { auto p = make({{ThroughZero, tz}}); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f); }
}
TEST_CASE("MD02 without Through zero the delay centres on Manual and the sweep is exponential") {
    for (double manual : {0.5, 3.0, 8.0}) {
        auto p = make({{ThroughZero, 0}, {Manual, manual}, {Depth, 0}, {Feedback, 0}});
        const auto y = run(p, impulse(0.1));
        CHECK(std::abs(double(peakAt(y, 1, y.size())) - manual * 0.001 * kFs) <= 1.0);
    }
    auto q = make({{ThroughZero, 0}, {Manual, 3.0}, {Depth, 70}, {Rate, 2.0}});
    std::vector<float> l(96000, 0.0f), r = l; double lo = 1e9, hi = 0;
    for (size_t off = 0; off < l.size(); off += 32) { float* c[2] = {l.data() + off, r.data() + off}; q.process(c, 2, 32); lo = std::min(lo, q.delaySamples(0)); hi = std::max(hi, q.delaySamples(0)); }
    NEAR(hi / lo, std::pow(2.0, 3.0 * 0.7), 0.1); NEAR(std::sqrt(hi * lo), 3.0 * 0.001 * kFs, 1.0);
}
TEST_CASE("MD02 Through zero: the delay crosses the dry signal's own") {
    auto p = make({{ThroughZero, 1}, {Manual, 3.0}, {Depth, 100}, {Rate, 2.0}, {Feedback, 0}});
    std::vector<float> l(96000, 0.0f), r = l; double lo = 1e9, hi = 0;
    for (size_t off = 0; off < l.size(); off += 32) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, 32); lo = std::min(lo, p.delaySamples(0)); hi = std::max(hi, p.delaySamples(0)); }
    NEAR(lo, 480.0 - 0.003 * kFs, 1.0); NEAR(hi, 480.0 + 0.003 * kFs, 1.0);   // 336 .. 624 samples around the 480 of the dry path
    CHECK(lo < 480.0); CHECK(hi > 480.0);
    // Depth 0: the wet sits exactly on the dry path
    auto q = make({{ThroughZero, 1}, {Depth, 0}, {Feedback, 0}});
    const auto y = run(q, impulse(0.1));
    CHECK(peakAt(y, 1, y.size()) == 480);
}
TEST_CASE("MD02 Feedback: positive and negative repeats at the delay") {
    for (double fb : {50.0, -50.0}) {
        auto p = make({{ThroughZero, 0}, {Manual, 2.0}, {Depth, 0}, {Feedback, fb}});
        const auto y = run(p, impulse(0.1));
        const size_t d = 96;
        const double a1 = y[d], a2 = y[2 * d], a3 = y[3 * d];
        NEAR(a1, 1.0, 0.02); NEAR(a2 / a1, fb * 0.01, 0.02); NEAR(a3 / a2, fb * 0.01, 0.02);
    }
    // 100 %: bounded with loud noise
    auto q = make({{ThroughZero, 0}, {Manual, 1.0}, {Feedback, 100}, {Depth, 100}});
    const auto z = run(q, noise(0, 4.0, 3)); for (float v : z) { CHECK(std::isfinite(v)); CHECK(std::abs(v) < 40.0f); }
}
TEST_CASE("MD02 Rate is in Hz; Sync turns it into note lengths at the host tempo") {
    NEAR(make({{Rate, 0.7}}).rateHz(), 0.7, 1e-9);
    NEAR(make({{Rate, 2.0}, {Sync, 1}}, 120).rateHz(), 2.0, 1e-9);           // 0.5 s = 1/4 at 120 bpm
    NEAR(make({{Rate, 2.0}, {Sync, 1}}, 90).rateHz(), 1.0 / noteSeconds(11, 90), 1e-9);   // the same note, slower
    NEAR(make({{Rate, 0.2}, {Sync, 1}}, 120).rateHz(), 0.25, 1e-9);            // 5 s -> 2 bars (4 s)
    NEAR(make({{Rate, 2.0}, {Sync, 1}}).rateHz(), 2.0, 1e-9);                  // no tempo: Hz
    // the sweep really runs at that rate
    auto p = make({{Rate, 2.0}, {Sync, 1}, {ThroughZero, 0}, {Manual, 3.0}, {Depth, 100}}, 90);
    std::vector<float> l(48000 * 10, 0.0f), r = l; int ups = 0; double prev = 0, centre = 3.0 * 0.001 * kFs;
    for (size_t off = 0; off < l.size(); off += 64) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, 64); const double d = p.delaySamples(0); if (prev < centre && d >= centre) ++ups; prev = d; }
    NEAR(ups / 10.0, 1.5, 0.11);   // 1/4 at 90 bpm = 0.667 s -> 1.5 Hz
}
TEST_CASE("MD02 with the host playing the sweep is on the bar grid") {
    auto p = make({{Rate, 2.0}, {Sync, 1}}, 120);   // 0.5 s period, 120 bpm: a bar line every 4 beats
    p.setTransport(true, 0.5);                      // 0.5 beat = 0.25 s to the bar line: half a period
    std::vector<float> l(1, 0.0f), r = l; float* c[2] = {l.data(), r.data()}; p.process(c, 2, 1);
    NEAR(p.lfoPhase(), 0.5 + 2.0 / kFs, 1e-6);       // half a period before the bar line: phase 0.5 (plus the one sample just run)
    auto q = make({{Rate, 2.0}, {Sync, 1}}, 120); q.setTransport(false, 0.5); q.process(c, 2, 1);
    NEAR(q.lfoPhase(), 2.0 / kFs, 1e-9);             // not playing: free running from 0
}
TEST_CASE("MD02 loud noise at the extremes stays finite") {
    for (double tz : {0.0, 1.0}) for (double fb : {-100.0, 100.0}) for (double m : {0.1, 10.0}) {
        auto p = make({{ThroughZero, tz}, {Feedback, fb}, {Manual, m}, {Depth, 100}, {Rate, 10}});
        const auto y = run(p, noise(0, 2.0, 4)); for (float v : y) { CHECK(std::isfinite(v)); CHECK(std::abs(v) < 40.0f); }
    }
}
