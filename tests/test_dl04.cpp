#include "doctest.h"
#include "dl04/dl04.hpp"
#include "sw/notes.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::dl04;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}, double bpm = 0.0) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); if (bpm > 0) p.setTempo(bpm); p.snapToTargets(); return p; }
// only the given taps on, level 0 dB, centre pan, filters open, no feedback, Sync off
Set only(std::vector<std::pair<int, double>> taps) {
    Set s = {{Feedback, 0}, {Sync, 0}, {PingPong, 0}};
    for (int t = 0; t < kTaps; ++t) { s.push_back({tapParam(t, On), 0}); s.push_back({tapParam(t, Level), 0}); s.push_back({tapParam(t, Pan), 0}); s.push_back({tapParam(t, Filter), 20000}); }
    for (auto& tp : taps) { s.push_back({tapParam(tp.first, On), 1}); s.push_back({tapParam(tp.first, Time), tp.second}); }
    return s;
}
std::vector<float> impulse(double sec) { std::vector<float> x(static_cast<size_t>(sec * kFs), 0.0f); x[0] = 1.0f; return x; }
size_t peakAt(const std::vector<float>& y, size_t a, size_t b) { size_t k = a; for (size_t i = a; i < std::min(b, y.size()); ++i) if (std::abs(y[i]) > std::abs(y[k])) k = i; return k; }
double peakAbs(const std::vector<float>& y, size_t a, size_t b) { double m = 0; for (size_t i = a; i < std::min(b, y.size()); ++i) m = std::max(m, double(std::abs(y[i]))); return m; }
}

TEST_CASE("DL04 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(kNumParams == 34);
    for (int t = 0; t < kTaps; ++t) {
        const std::string pre = "dl04.tap" + std::to_string(t + 1) + ".";
        CHECK(std::string(s[static_cast<size_t>(tapParam(t, On))].id) == pre + "on");
        CHECK(std::string(s[static_cast<size_t>(tapParam(t, Time))].id) == pre + "time");
        CHECK(std::string(s[static_cast<size_t>(tapParam(t, Level))].id) == pre + "level");
        CHECK(std::string(s[static_cast<size_t>(tapParam(t, Pan))].id) == pre + "pan");
        CHECK(std::string(s[static_cast<size_t>(tapParam(t, Filter))].id) == pre + "filter");
        CHECK(s[static_cast<size_t>(tapParam(t, On))].def == (t < 3 ? 1.0 : 0.0));
        CHECK(s[static_cast<size_t>(tapParam(t, Time))].min == 1); CHECK(s[static_cast<size_t>(tapParam(t, Time))].max == 4000); CHECK(s[static_cast<size_t>(tapParam(t, Time))].curve == Curve::Log);
        CHECK(s[static_cast<size_t>(tapParam(t, Level))].min == -60); CHECK(s[static_cast<size_t>(tapParam(t, Level))].max == 0); CHECK(s[static_cast<size_t>(tapParam(t, Level))].def == -6);
        CHECK(s[static_cast<size_t>(tapParam(t, Pan))].min == -100); CHECK(s[static_cast<size_t>(tapParam(t, Pan))].max == 100);
        CHECK(s[static_cast<size_t>(tapParam(t, Pan))].def == ((t & 1) ? 50.0 : -50.0));   // left and right in turn
        CHECK(s[static_cast<size_t>(tapParam(t, Filter))].min == 200); CHECK(s[static_cast<size_t>(tapParam(t, Filter))].max == 20000); CHECK(s[static_cast<size_t>(tapParam(t, Filter))].def == 8000);
    }
    // 1/8, 1/8 D, 1/4 at 120 bpm
    CHECK(s[static_cast<size_t>(tapParam(0, Time))].def == 250); CHECK(s[static_cast<size_t>(tapParam(1, Time))].def == 375); CHECK(s[static_cast<size_t>(tapParam(2, Time))].def == 500);
    CHECK(std::string(s[Feedback].id) == "dl04.feedback"); CHECK(s[Feedback].def == 30); CHECK(s[Feedback].max == 100);
    CHECK(s[Mix].def == 20); CHECK(s[Sync].def == 1); CHECK(s[PingPong].def == 0);
}
TEST_CASE("DL04 no delay is reported; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("DL04 each tap sits at its own time with its own level") {
    Set s = only({{0, 100}, {1, 250}, {4, 700}});
    s.push_back({tapParam(1, Level), -6}); s.push_back({tapParam(4, Level), -12});
    auto p = make(s); const auto y = run(p, impulse(1.5));
    const double sc = std::sqrt(0.5);   // centre pan, constant power
    const size_t d[3] = {4800, 12000, 33600};
    NEAR(peakAbs(y, d[0] - 2, d[0] + 3), sc * 1.0, 0.02);
    NEAR(peakAbs(y, d[1] - 2, d[1] + 3), sc * std::pow(10.0, -6 / 20.0), 0.02);
    NEAR(peakAbs(y, d[2] - 2, d[2] + 3), sc * std::pow(10.0, -12 / 20.0), 0.02);
    for (int k = 0; k < 3; ++k) CHECK(peakAt(y, d[k] - 5, d[k] + 6) == d[k]);
    CHECK(peakAbs(y, 0, 4700) < 1e-9); CHECK(peakAbs(y, 4900, 11900) < 1e-9); CHECK(peakAbs(y, 12100, 33500) < 1e-9);   // nothing else (Feedback 0)
}
TEST_CASE("DL04 Pan places a tap with constant power") {
    for (double pan : {-100.0, -50.0, 0.0, 50.0, 100.0}) {
        Set s = only({{0, 100}}); s.push_back({tapParam(0, Pan), pan});
        auto p = make(s); auto l = impulse(0.3); auto r = l; const auto o = run2(p, l, r);
        const double pl = peakAbs(o.first, 4795, 4806), pr = peakAbs(o.second, 4795, 4806);
        NEAR(pl * pl + pr * pr, 1.0, 0.01);
        if (pan == -100.0) { NEAR(pl, 1.0, 0.01); CHECK(pr < 1e-6); }
        if (pan == 100.0) { NEAR(pr, 1.0, 0.01); CHECK(pl < 1e-6); }
        if (pan == 0.0) NEAR(pl, pr, 1e-6);
        if (pan > 0) CHECK(pr > pl);
        if (pan < 0) CHECK(pl > pr);
    }
}
TEST_CASE("DL04 Filter takes the highs off one tap, not the others") {
    Set s = only({{0, 100}, {1, 200}}); s.push_back({tapParam(0, Filter), 1000});
    auto p = make(s); auto x = sine(-12, 0.1, 6000); x.resize(static_cast<size_t>(0.6 * kFs), 0.0f);
    const auto y = run(p, x);
    const double t1 = rmsDb(y, 4800 + 500, 4800 + 4000), t2 = rmsDb(y, 9600 + 500, 9600 + 4000);
    CHECK(t1 < t2 - 20.0);
}
TEST_CASE("DL04 Feedback repeats the whole pattern; 100 % stays bounded") {
    Set s = only({{0, 100}}); s.push_back({Feedback, 40});
    auto p = make(s); const auto y = run(p, impulse(1.0));
    const double a1 = peakAbs(y, 4795, 4806), a2 = peakAbs(y, 9595, 9606), a3 = peakAbs(y, 14395, 14406);
    NEAR(a2 / a1, 0.4, 0.01); NEAR(a3 / a2, 0.4, 0.01);
    // all six taps, 100 % feedback, loud noise: no runaway
    Set b = {{Feedback, 100}, {Sync, 0}}; for (int t = 0; t < kTaps; ++t) { b.push_back({tapParam(t, On), 1}); b.push_back({tapParam(t, Level), 0}); }
    auto q = make(b); auto x = noise(-6, 0.5, 4); x.resize(static_cast<size_t>(10 * kFs), 0.0f);
    const auto z = run(q, x); double m = 0; for (float v : z) { CHECK(std::isfinite(v)); m = std::max(m, double(std::abs(v))); }
    CHECK(m < 40.0);
}
TEST_CASE("DL04 Ping-pong swaps the sides on every trip") {
    Set s = only({{0, 100}}); s.push_back({tapParam(0, Pan), -100}); s.push_back({Feedback, 60});
    auto off = make(s); s.push_back({PingPong, 1}); auto on = make(s);
    auto l = impulse(1.0); auto r = l;
    const auto a = run2(off, l, r), b = run2(on, l, r);
    for (size_t k = 1; k <= 3; ++k) { CHECK(peakAbs(a.first, k * 4800 - 5, k * 4800 + 6) > 0.1); CHECK(peakAbs(a.second, k * 4800 - 5, k * 4800 + 6) < 1e-6); }   // Off: always left
    CHECK(peakAbs(b.first, 4795, 4806) > 0.5);  CHECK(peakAbs(b.second, 4795, 4806) < 1e-6);                                                                 // 1st: left (as set)
    CHECK(peakAbs(b.second, 9595, 9606) > 0.3); CHECK(peakAbs(b.first, 9595, 9606) < 1e-6);                                                                  // 2nd: right
    CHECK(peakAbs(b.first, 14395, 14406) > 0.15); CHECK(peakAbs(b.second, 14395, 14406) < 1e-6);                                                             // 3rd: left
}
TEST_CASE("DL04 Sync moves each Time to the nearest note at the host tempo") {
    // the default times are 1/8, 1/8 D and 1/4 at 120 bpm
    auto p = make({}, 120);
    NEAR(p.tapSeconds(0), 0.25, 1e-9); NEAR(p.tapSeconds(1), 0.375, 1e-9); NEAR(p.tapSeconds(2), 0.5, 1e-9);
    // at 90 bpm they follow the tempo
    auto q = make({}, 90);
    NEAR(q.tapSeconds(0), noteSeconds(8, 90), 1e-9); NEAR(q.tapSeconds(1), noteSeconds(10, 90), 1e-9); NEAR(q.tapSeconds(2), noteSeconds(11, 90), 1e-9);
    // without a tempo, and with Sync off, they stay in ms
    auto n = make(); NEAR(n.tapSeconds(0), 0.25, 1e-9);
    auto o = make({{Sync, 0}}, 90); NEAR(o.tapSeconds(1), 0.375, 1e-9);
    // 4 s at most
    auto big = make({{tapParam(5, Time), 4000}}, 40); CHECK(big.tapSeconds(5) <= 4.0 + 1e-12);
}
TEST_CASE("DL04 a tap switching on or off fades (no click)") {
    Set s = only({{0, 50}});
    auto p = make(s); auto dc = std::vector<float>(48000, 0.1f); auto r = dc;
    for (size_t off = 0; off < 24000; off += 256) { const int n = static_cast<int>(std::min<size_t>(256, 24000 - off)); float* c[2] = {dc.data() + off, r.data() + off}; p.process(c, 2, n); }
    p.setParam(tapParam(0, On), 0);
    for (size_t off = 24000; off < 48000; off += 256) { const int n = static_cast<int>(std::min<size_t>(256, 48000 - off)); float* c[2] = {dc.data() + off, r.data() + off}; p.process(c, 2, n); }
    double maxStep = 0; for (size_t i = 24001; i < 30000; ++i) maxStep = std::max(maxStep, double(std::abs(dc[i] - dc[i - 1])));
    CHECK(maxStep < 0.1 * 0.8 / (0.01 * kFs) * 3.0);   // a 10 ms ramp: the step per sample is a fraction of the level
    CHECK(std::abs(dc[47000]) < 1e-6);
}
