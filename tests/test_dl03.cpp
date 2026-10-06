#include "doctest.h"
#include "dl03/dl03.hpp"
#include "sw/notes.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::dl03;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}, double bpm = 0.0) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); if (bpm > 0) p.setTempo(bpm); p.snapToTargets(); return p; }
Set clean(double ms, double fb = 0) { return {{Time, ms}, {Feedback, fb}, {ModDepth, 0}, {Grit, 0}, {Sync, 0}}; }
double peakAbs(const std::vector<float>& y, size_t a, size_t b) { double m = 0; for (size_t i = a; i < std::min(b, y.size()); ++i) m = std::max(m, double(std::abs(y[i]))); return m; }
std::vector<float> toneBurst(double f, double db, double burstSec, double total) { auto x = sine(db, burstSec, f); x.resize(static_cast<size_t>(total * kFs), 0.0f); return x; }
// level of a tone in the echo (the window sits well inside the echo of a burst that is long enough)
double echoDb(double ms, double f, double db, Set extra = {}) {
    Set s = clean(ms); for (auto& e : extra) s.push_back(e);
    auto p = make(s); const auto y = run(p, toneBurst(f, db, 0.5, 2.2));
    const size_t a = static_cast<size_t>((ms * 0.001 + 0.15) * kFs), b = static_cast<size_t>((ms * 0.001 + 0.4) * kFs);
    return rmsDb(y, a, b);
}
}

TEST_CASE("DL03 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"dl03.time", "dl03.feedback", "dl03.moddepth", "dl03.modrate", "dl03.grit", "dl03.mix", "dl03.sync"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Time].min == 20); CHECK(s[Time].max == 600); CHECK(s[Time].def == 300); CHECK(s[Time].curve == Curve::Log);
    CHECK(s[Feedback].def == 4); CHECK(s[Feedback].max == 10);
    CHECK(s[ModDepth].def == 2); CHECK(s[ModRate].def == 3); CHECK(s[Grit].def == 2); CHECK(s[Mix].def == 25); CHECK(s[Sync].def == 0);
}
TEST_CASE("DL03 no delay is reported; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("DL03 the clock follows the delay: longer is slower") {
    NEAR(make(clean(300)).clockHz(), 4096.0 / 0.3, 1e-6);
    NEAR(make(clean(600)).clockHz(), 4096.0 / 0.6, 1e-6);
    NEAR(make(clean(20)).clockHz(), kFs, 1e-6);   // limited to the host rate: a short delay is a plain one
    CHECK(make(clean(100)).clockHz() > make(clean(200)).clockHz());
}
TEST_CASE("DL03 the echo comes back after Time") {
    for (double ms : {25.0, 80.0, 300.0, 600.0}) {
        auto p = make(clean(ms));
        std::vector<float> x(static_cast<size_t>(1.0 * kFs), 0.0f); x[0] = 0.5f;
        const auto y = run(p, x);
        const size_t d = static_cast<size_t>(ms * 0.001 * kFs);
        size_t k = 100; for (size_t i = 100; i < y.size(); ++i) if (std::abs(y[i]) > std::abs(y[k])) k = i;
        CHECK(std::abs(double(k) - double(d)) < 0.0015 * kFs);   // the filters add a little group delay, the clock grid a few samples
        CHECK(peakAbs(y, 0, d / 2) < 1e-6);
    }
}
TEST_CASE("DL03 a longer Time narrows the band") {
    const double short40 = echoDb(40, 5000, -24), long300 = echoDb(300, 5000, -24), long600 = echoDb(600, 5000, -24);
    CHECK(short40 > long300 + 10.0); CHECK(long300 > long600 + 6.0);
    NEAR(echoDb(300, 300, -24), echoDb(40, 300, -24), 1.0);   // the low end is the same
}
TEST_CASE("DL03 the clock folds high tones back (aliasing grows with Time)") {
    // 7 kHz against the 6.8 kHz clock of 600 ms: the difference tone (about 170 Hz) appears in the echo
    auto alias = [&](double ms) { Set s = clean(ms); auto p = make(s); const auto y = run(p, toneBurst(7000, -12, 0.5, 2.2)); const size_t a = static_cast<size_t>((ms * 0.001 + 0.15) * kFs), b = static_cast<size_t>((ms * 0.001 + 0.4) * kFs); return binDb(y, 4096.0 / (ms * 0.001) - 7000.0, a, b); };
    CHECK(alias(600) > alias(40) + 12.0); CHECK(alias(600) > -60.0);   // measured -45 dB (the 12-dB-per-octave pre-filter lets it through)
}
TEST_CASE("DL03 Feedback repeats the echo (0 .. 110 %)") {
    for (double fbv : {3.0, 6.0, 9.0}) {
        Set s = clean(100, fbv); auto p = make(s);
        const auto y = run(p, toneBurst(300, -24, 0.3, 3.0));
        const size_t d = 4800;
        const double r = rmsDb(y, 3 * d + 4800, 3 * d + 12000) - rmsDb(y, 2 * d + 4800, 2 * d + 12000);
        NEAR(r, 20 * std::log10(fbv * 0.11), 2.0);
    }
    for (double ms : {40.0, 300.0}) {
        Set s = clean(ms, 10); auto p = make(s); auto x = noise(-12, 0.3, 4); x.resize(static_cast<size_t>(20 * kFs), 0.0f);
        const auto y = run(p, x); double m = 0; for (float v : y) { CHECK(std::isfinite(v)); m = std::max(m, double(std::abs(v))); }
        CHECK(m < 4.0); CHECK(peakAbs(y, y.size() - 48000, y.size()) > 0.01);
    }
}
TEST_CASE("DL03 Mod depth wobbles the pitch of the echo") {
    auto carrier = [&](double depth) { Set s = clean(200); s.push_back({ModDepth, depth}); s.push_back({ModRate, 6}); auto p = make(s); const auto y = run(p, sine(-24, 3.0, 1000)); return binDb(y, 1000, 24000, 140000) - rmsDb(y, 24000, 140000); };
    CHECK(carrier(0) > -0.5); CHECK(carrier(10) < carrier(0) - 3.0);
}
TEST_CASE("DL03 Grit adds the BBD's fizz to the echoes") {
    auto floorDb = [&](double grit) { Set s = clean(100); s.push_back({Grit, grit}); auto p = make(s); const auto y = run(p, sine(-24, 2.0, 300)); const size_t a = 48000, b = 96000; return binDb(y, 2000, a, b) - binDb(y, 300, a, b); };
    CHECK(floorDb(0) < -60.0);
    CHECK(floorDb(10) > floorDb(0) + 15.0);
}
TEST_CASE("DL03 Sync moves Time to the nearest note at the host tempo, inside 20 .. 600 ms") {
    for (double bpm : {70.0, 100.0, 120.0, 150.0}) for (double ms : {20.0, 90.0, 300.0, 600.0}) {
        Set s = clean(ms); s.push_back({Sync, 1});
        auto p = make(s, bpm); const double t = p.timeSeconds();
        CHECK(t >= 0.02 - 1e-12); CHECK(t <= 0.6 + 1e-12);
        const double want = std::clamp(noteSeconds(noteNearest(ms * 0.001, bpm), bpm), 0.02, 0.6);
        NEAR(t, want, 1e-12);
    }
    NEAR(make([] { Set s = clean(300); s.push_back({Sync, 1}); return s; }(), 120).timeSeconds(), 1.0 / 3.0, 1e-9);   // 300 ms -> 1/4 triplet (333 ms)
    NEAR(make([] { Set s = clean(300); s.push_back({Sync, 1}); return s; }()).timeSeconds(), 0.3, 1e-9);                // no tempo: ms
}
TEST_CASE("DL03 loud noise at the extremes stays finite and bounded") {
    for (double ms : {20.0, 600.0}) for (double g : {0.0, 10.0}) {
        Set s = clean(ms, 10); s.push_back({Grit, g}); s.push_back({ModDepth, 10}); s.push_back({ModRate, 10});
        auto p = make(s); const auto y = run(p, noise(0, 2.0, 5));
        for (float v : y) { CHECK(std::isfinite(v)); CHECK(std::abs(v) < 20.0f); }
    }
}
