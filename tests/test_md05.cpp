#include "doctest.h"
#include "md05/md05.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::md05;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
void go(Processor& p, std::vector<float>& l, std::vector<float>& r) { for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } }
std::vector<double> envelope(const std::vector<float>& y, size_t win, size_t from = 0) { std::vector<double> e; for (size_t a = from; a + win <= y.size(); a += win) { double m = 0; for (size_t i = a; i < a + win; ++i) m = std::max(m, double(std::abs(y[i]))); e.push_back(m); } return e; }
void run0(Processor& p, double seconds) { std::vector<float> l(static_cast<size_t>(seconds * kFs), 0.0f), r = l; go(p, l, r); }
}

TEST_CASE("MD05 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"md05.speed", "md05.accel", "md05.horn", "md05.drum", "md05.micdist", "md05.drive", "md05.mix"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Speed].labels == std::vector<std::string>{"Stop", "Slow", "Fast"}); CHECK(s[Speed].def == 1);
    CHECK(s[Accel].def == 5); CHECK(s[Horn].def == 7); CHECK(s[Drum].def == 7); CHECK(s[MicDistance].def == 50); CHECK(s[Drive].def == 2); CHECK(s[Mix].def == 100);
}
TEST_CASE("MD05 the reported latency is the 1 ms Doppler centre (48 samples at 48 kHz), fixed") {
    { Processor p; CHECK(p.latencySamples() == 48); }
    { Processor p; p.prepare(96000.0, 256); CHECK(p.latencySamples() == 96); }
    { Processor p; p.prepare(kFs, 256); p.setParam(Speed, 2); p.setParam(Accel, 0); CHECK(p.latencySamples() == 48); }
}
TEST_CASE("MD05 silence is silence") {
    for (double sp : {0.0, 1.0, 2.0}) { auto p = make({{Speed, sp}}); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f); }
}
TEST_CASE("MD05 the rotors turn at their speeds, and the drum is slower to follow") {
    for (int sp : {1, 2}) {
        auto p = make({{Speed, double(sp)}, {Accel, 5}}); run0(p, 40.0);
        NEAR(p.hornHz(), hornTargetHz(sp), 1e-3); NEAR(p.drumHz(), drumTargetHz(sp), 1e-3);
    }
    NEAR(hornTargetHz(1), 0.8, 1e-12); NEAR(hornTargetHz(2), 6.7, 1e-12); NEAR(drumTargetHz(1), 0.67, 1e-12); NEAR(drumTargetHz(2), 5.7, 1e-12);
    // Slow -> Fast: after one time constant 63 % of the way; the drum takes 3 times as long
    auto p = make({{Speed, 1}, {Accel, 5}});
    p.setParam(Speed, 2);
    run0(p, hornTau(5));
    NEAR((p.hornHz() - 0.8) / (6.7 - 0.8), 1.0 - std::exp(-1.0), 0.02);
    NEAR((p.drumHz() - 0.67) / (5.7 - 0.67), 1.0 - std::exp(-1.0 / 3.0), 0.02);
    // Accel: 0 is quick, 10 slow
    NEAR(hornTau(0), 0.3, 1e-12); NEAR(hornTau(10), 2.3, 1e-12); NEAR(drumTau(5), 3.0 * hornTau(5), 1e-12);
    auto q = make({{Speed, 1}, {Accel, 0}}); q.setParam(Speed, 2); run0(q, 1.0); auto w = make({{Speed, 1}, {Accel, 10}}); w.setParam(Speed, 2); run0(w, 1.0);
    CHECK(q.hornHz() > 6.0); CHECK(w.hornHz() < 3.0);
    // Stop: the speeds die away
    auto st = make({{Speed, 2}, {Accel, 0}}); st.setParam(Speed, 0); run0(st, 20.0); CHECK(st.hornHz() < 1e-3); CHECK(st.drumHz() < 1e-3);
}
TEST_CASE("MD05 Doppler: the delay swings by R / c around 1 ms") {
    auto p = make({{Speed, 2}});
    std::vector<float> l(96000, 0.0f), r = l; double hLo = 1e9, hHi = 0, dLo = 1e9, dHi = 0;
    for (size_t off = 0; off < l.size(); off += 32) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, 32); hLo = std::min(hLo, p.dopplerSamples(true, 0)); hHi = std::max(hHi, p.dopplerSamples(true, 0)); dLo = std::min(dLo, p.dopplerSamples(false, 0)); dHi = std::max(dHi, p.dopplerSamples(false, 0)); }
    NEAR(0.5 * (hLo + hHi), 48.0, 0.3); NEAR(0.5 * (hHi - hLo), 0.18 / 343.0 * kFs, 0.3);   // +-25 samples
    NEAR(0.5 * (dLo + dHi), 48.0, 0.3); NEAR(0.5 * (dHi - dLo), 0.12 / 343.0 * kFs, 0.3);
}
TEST_CASE("MD05 amplitude: the horn throbs at its speed, harder at Near than at Far; the level stays near 0 dB") {
    auto swing = [&](double mic, double sp, double f) {
        auto p = make({{Speed, sp}, {MicDistance, mic}, {Drive, 0}, {Accel, 0}});
        auto l = sine(-18, 8.0, f), r = l; go(p, l, r);
        const auto e = envelope(l, 96, 96000); double lo = 1e9, hi = 0; for (double v : e) { lo = std::min(lo, v); hi = std::max(hi, v); }
        return hi / lo;
    };
    const double near = swing(0, 2, 4000), far = swing(100, 2, 4000);
    CHECK(near > 2.0);   // 1 / (1 - 0.7) = 3.3 for a mic facing the horn's sweep; the two mics differ, one is enough
    CHECK(far < near); CHECK(far > 1.2);
    // the drum swings less than the horn
    CHECK(swing(0, 2, 200) < near);
    // level over a long noise: close to the input (the mean gain is 1)
    auto p = make({{Speed, 2}, {Drive, 0}, {Accel, 0}}); auto l = noise(-18, 6.0, 5), r = l; go(p, l, r);
    const double in = -18.0; NEAR(rmsDb(l, 48000, 288000), in, 2.5);
}
TEST_CASE("MD05 Stop leaves the sound still") {
    auto p = make({{Speed, 0}, {Drive, 0}}); run0(p, 1.0);
    auto l = sine(-18, 4.0, 1000), r = l; go(p, l, r);
    const auto e = envelope(l, 480, 48000); double lo = 1e9, hi = 0; for (double v : e) { lo = std::min(lo, v); hi = std::max(hi, v); }
    CHECK(hi / lo < 1.01);
}
TEST_CASE("MD05 Horn and Drum volumes; Drive adds harmonics") {
    auto level = [&](double horn, double drum, double f) { auto p = make({{Speed, 0}, {Horn, horn}, {Drum, drum}, {Drive, 0}}); auto l = sine(-18, 2.0, f), r = l; go(p, l, r); return rmsDb(l, 48000, 96000); };
    NEAR(level(10, 7, 4000) - level(7, 7, 4000), 20 * std::log10(10.0 / 7.0), 0.1);
    CHECK(level(0, 7, 4000) < level(7, 7, 4000) - 45.0);   // the other band leaks only through the crossover slope
    NEAR(level(7, 10, 200) - level(7, 7, 200), 20 * std::log10(10.0 / 7.0), 0.1);
    CHECK(level(7, 0, 200) < level(7, 7, 200) - 45.0);
    auto thd = [&](double drive) { auto p = make({{Speed, 0}, {Drive, drive}}); auto l = sine(-6, 2.0, 1000), r = l; go(p, l, r); return binDb(l, 3000, 48000, 96000) - binDb(l, 1000, 48000, 96000); };
    CHECK(thd(10) > thd(0) + 15.0);   // measured -27 -> -9 dB
}
TEST_CASE("MD05 extremes stay finite") {
    for (double sp : {0.0, 2.0}) for (double a : {0.0, 10.0}) for (double mic : {0.0, 100.0}) {
        auto p = make({{Speed, sp}, {Accel, a}, {MicDistance, mic}, {Drive, 10}, {Horn, 10}, {Drum, 10}});
        auto l = noise(0, 2.0, 4), r = l; go(p, l, r); for (float v : l) { CHECK(std::isfinite(v)); CHECK(std::abs(v) < 20.0f); }
    }
}
