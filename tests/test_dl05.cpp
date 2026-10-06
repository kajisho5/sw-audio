#include "doctest.h"
#include "dl05/dl05.hpp"
#include "sw/notes.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::dl05;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
// 120 bpm: Time index 6 = 1/4 = 0.5 s = 24000 samples
Processor make(Set set = {}, double bpm = 120.0) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.setTempo(bpm); p.snapToTargets(); return p; }
std::vector<float> clickAt(double sec, double at) { std::vector<float> x(static_cast<size_t>(sec * kFs), 0.0f); x[static_cast<size_t>(at * kFs)] = 1.0f; return x; }
size_t peakAt(const std::vector<float>& y, size_t a, size_t b) { size_t k = a; for (size_t i = a; i < std::min(b, y.size()); ++i) if (std::abs(y[i]) > std::abs(y[k])) k = i; return k; }
double peakAbs(const std::vector<float>& y, size_t a, size_t b) { double m = 0; for (size_t i = a; i < std::min(b, y.size()); ++i) m = std::max(m, double(std::abs(y[i]))); return m; }
}

TEST_CASE("DL05 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"dl05.mode", "dl05.time", "dl05.grain", "dl05.spray", "dl05.pitch", "dl05.freeze", "dl05.mix"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Mode].labels == std::vector<std::string>{"Reverse", "Forward", "Random"}); CHECK(s[Mode].def == 0);
    REQUIRE(s[Time].labels.size() == static_cast<size_t>(kNumTimes));
    CHECK(s[Time].labels.front() == "1/16"); CHECK(s[Time].labels.back() == "2 bars"); CHECK(s[Time].labels[static_cast<size_t>(s[Time].def)] == "1/4");
    CHECK(s[GrainSize].min == 10); CHECK(s[GrainSize].max == 500); CHECK(s[GrainSize].def == 80); CHECK(s[GrainSize].curve == Curve::Log);
    CHECK(s[Spray].def == 30); CHECK(s[Pitch].def == 0); CHECK(s[Freeze].def == 0); CHECK(s[Mix].def == 40);
}
TEST_CASE("DL05 no delay is reported; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("DL05 Time follows the host tempo") {
    NEAR(make({}, 120).timeSamples(), 0.5 * kFs, 1e-6);
    NEAR(make({}, 90).timeSamples(), noteSeconds(11, 90) * kFs, 1e-6);
    NEAR(make({}, 0).timeSamples(), 0.5 * kFs, 1e-6);   // no tempo: 120 bpm
    NEAR(make({{Time, 12}}, 40).timeSamples(), 4.0 * kFs, 1e-6);   // 2 bars at 40 bpm = 12 s, capped
    NEAR(make({{Time, 0}}, 120).timeSamples(), 0.125 * kFs, 1e-6);   // 1/16
}
TEST_CASE("DL05 Forward is a plain delay of one note, whatever the grain size") {
    for (double grain : {10.0, 80.0, 500.0}) {
        auto p = make({{Mode, Forward}, {Spray, 0}, {GrainSize, grain}});
        const auto y = run(p, clickAt(1.5, 0.2));
        const size_t d = static_cast<size_t>(0.7 * kFs);
        CHECK(peakAt(y, 0, y.size()) == d);
        NEAR(peakAbs(y, d - 1, d + 2), 1.0, 0.02);
    }
}
TEST_CASE("DL05 Reverse plays the last segment backwards") {
    // free running boundaries at k P (P = 0.5 s): a click at 0.3 s is heard at 2 x 0.5 - 0.3 = 0.7 s; a click at 0.1 s at 0.9 s
    for (double at : {0.1, 0.3}) {
        auto p = make({{Mode, Reverse}, {Spray, 0}, {GrainSize, 80}});
        const auto y = run(p, clickAt(1.5, at));
        const size_t want = static_cast<size_t>((1.0 - at) * kFs);
        CHECK(std::abs(double(peakAt(y, 1, y.size())) - double(want)) <= 2.0);
        // nothing before the boundary at 0.5 s (apart from the click's own passage through the first grain)
        CHECK(peakAbs(y, 0, static_cast<size_t>(0.5 * kFs) - 4) < 0.05 + (at > 0 ? 0.0 : 0.0));
    }
    // two clicks 0.1 s apart come out in the opposite order: the click that was later is heard first
    auto q = make({{Mode, Reverse}, {Spray, 0}});
    auto x = clickAt(1.5, 0.2); x[static_cast<size_t>(0.3 * kFs)] = 0.5f;
    const auto y = run(q, x);
    const size_t a = peakAt(y, static_cast<size_t>(0.6 * kFs), static_cast<size_t>(0.75 * kFs)), b = peakAt(y, static_cast<size_t>(0.75 * kFs), static_cast<size_t>(0.9 * kFs));
    CHECK(std::abs(double(a) - 0.7 * kFs) <= 2.0); CHECK(std::abs(double(b) - 0.8 * kFs) <= 2.0);   // 0.3 s -> 0.7 s (amplitude 0.5), 0.2 s -> 0.8 s (amplitude 1)
    CHECK(std::abs(y[a]) < std::abs(y[b]));
}
TEST_CASE("DL05 on the bar grid: the host's bar line sets the segment boundaries") {
    // 120 bpm, P = 0.5 s = 24000 samples. Next bar line 0.3 beat = 7200 samples ahead: boundaries at 7200 + 24000 k
    auto p = make({{Mode, Reverse}, {Spray, 0}, {GrainSize, 80}});
    p.setTransport(true, 0.3);
    const auto y = run(p, clickAt(1.5, 0.3));   // sample 14400: segment boundary B = 31200 -> heard at 2 B - 14400 = 48000
    CHECK(std::abs(double(peakAt(y, 20000, y.size())) - 48000.0) <= 2.0);
    // not playing: the free-running grid (boundary at 24000 -> 33600)
    auto q = make({{Mode, Reverse}, {Spray, 0}, {GrainSize, 80}}); q.setTransport(false, 0.3);
    const auto z = run(q, clickAt(1.5, 0.3));
    CHECK(std::abs(double(peakAt(z, 20000, z.size())) - 33600.0) <= 2.0);
}
TEST_CASE("DL05 Pitch +12 doubles the frequency") {
    auto x = sine(-12, 3.0, 440);
    auto off = make({{Mode, Forward}, {Spray, 0}, {Pitch, 0}}); auto on = make({{Mode, Forward}, {Spray, 0}, {Pitch, 1}});
    const auto a = run(off, x), b = run(on, x);
    CHECK(binDb(a, 440, 96000, 144000) > binDb(a, 880, 96000, 144000) + 30.0);
    CHECK(binDb(b, 880, 96000, 144000) > binDb(b, 440, 96000, 144000) + 20.0);
}
TEST_CASE("DL05 Pitch +12 in Reverse reads backwards at twice the speed") {
    auto x = sine(-12, 3.0, 440);
    auto p = make({{Mode, Reverse}, {Spray, 0}, {Pitch, 1}});
    const auto y = run(p, x);
    CHECK(binDb(y, 880, 96000, 144000) > binDb(y, 440, 96000, 144000) + 15.0);
}
TEST_CASE("DL05 Spray scatters the grains") {
    auto run1 = [&](double spray) { auto p = make({{Mode, Forward}, {Spray, spray}, {GrainSize, 80}}); const auto y = run(p, clickAt(1.5, 0.2)); return std::make_pair(peakAbs(y, 0, y.size()), y); };
    const auto a = run1(0), b = run1(100);
    CHECK(a.first > 0.95); CHECK(b.first < 0.9 * a.first);
}
TEST_CASE("DL05 Random keeps the level and moves around") {
    auto x = noise(-18, 4.0, 3);
    auto fwd = make({{Mode, Forward}, {Spray, 0}}); auto rnd = make({{Mode, Random}, {Spray, 0}});
    const auto a = run(fwd, x), b = run(rnd, x);
    NEAR(rmsDb(b, 48000, 192000), rmsDb(a, 48000, 192000), 3.0);
    for (float v : b) CHECK(std::isfinite(v));
    // not a plain delay: correlation with the delayed input is low
    double c = 0, e = 0; for (size_t i = 60000; i < 190000; ++i) { c += double(b[i]) * x[i - 24000]; e += double(b[i]) * b[i]; }
    CHECK(std::abs(c) / (e + 1e-12) < 0.5);
}
TEST_CASE("DL05 Freeze keeps the last note going") {
    auto x = noise(-18, 2.0, 5); x.resize(static_cast<size_t>(10 * kFs), 0.0f);
    auto run2f = [&](bool freezeOn) {
        auto p = make({{Mode, Reverse}, {Spray, 0}});
        auto l = x; auto r = x;
        auto go = [&](size_t a, size_t b) { for (size_t off = a; off < b; off += 256) { const int n = static_cast<int>(std::min<size_t>(256, b - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } };
        go(0, 96000);
        if (freezeOn) p.setParam(Freeze, 1);
        go(96000, l.size());
        return l;
    };
    const auto f = run2f(true), nf = run2f(false);
    CHECK(rmsDb(f, 8 * 48000, 10 * 48000) > -30.0);     // still there six seconds after the input stopped (and 12 note lengths on)
    CHECK(rmsDb(nf, 8 * 48000, 10 * 48000) < -100.0);
    for (float v : f) CHECK(std::isfinite(v));
}
TEST_CASE("DL05 extremes stay finite") {
    for (int mode = 0; mode < 3; ++mode) for (double grain : {10.0, 500.0}) for (double t : {0.0, 12.0}) {
        auto p = make({{Mode, double(mode)}, {GrainSize, grain}, {Time, t}, {Spray, 100}, {Pitch, 1}});
        const auto y = run(p, noise(0, 3.0, 8)); for (float v : y) { CHECK(std::isfinite(v)); CHECK(std::abs(v) < 8.0f); }
    }
}
