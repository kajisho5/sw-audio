#include "doctest.h"
#include "dl01/dl01.hpp"
#include "sw/notes.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::dl01;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}, double bpm = 0.0) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); if (bpm > 0) p.setTempo(bpm); p.snapToTargets(); return p; }
// Digital, no filters, no modulation: the plain delay
Set plain(double timeMs, double fb) { return {{Mode, Digital}, {Time, timeMs}, {Feedback, fb}, {Hpf, 20}, {Lpf, 20000}, {Depth, 0}, {Sync, 0}}; }
std::vector<float> impulse(double sec) { std::vector<float> x(static_cast<size_t>(sec * kFs), 0.0f); x[0] = 1.0f; return x; }
size_t peakAt(const std::vector<float>& y, size_t a, size_t b) { size_t k = a; for (size_t i = a; i < std::min(b, y.size()); ++i) if (std::abs(y[i]) > std::abs(y[k])) k = i; return k; }
double peakAbs(const std::vector<float>& y, size_t a, size_t b) { double m = 0; for (size_t i = a; i < std::min(b, y.size()); ++i) m = std::max(m, double(std::abs(y[i]))); return m; }
}

TEST_CASE("DL01 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"dl01.mode", "dl01.time", "dl01.feedback", "dl01.hpf", "dl01.lpf", "dl01.depth", "dl01.rate", "dl01.duck", "dl01.mix", "dl01.sync", "dl01.pingpong", "dl01.unit"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Mode].labels == std::vector<std::string>{"Tape", "Analog", "Digital"}); CHECK(s[Mode].def == 0);
    CHECK(s[Time].min == 1); CHECK(s[Time].max == 2000); CHECK(s[Time].def == 375); CHECK(s[Time].curve == Curve::Log);
    CHECK(s[Feedback].max == 110); CHECK(s[Feedback].def == 35);
    CHECK(s[Hpf].min == 20); CHECK(s[Hpf].max == 1000); CHECK(s[Hpf].def == 100); CHECK(std::string(s[Hpf].minLabel) == "Off");
    CHECK(s[Lpf].min == 1000); CHECK(s[Lpf].max == 20000); CHECK(s[Lpf].def == 8000); CHECK(std::string(s[Lpf].maxLabel) == "Off");
    CHECK(s[Depth].def == 10); CHECK(s[Rate].min == 0.1); CHECK(s[Rate].max == 10); CHECK(s[Rate].def == 0.5);
    CHECK(s[Duck].max == 20); CHECK(s[Duck].def == 0); CHECK(s[Mix].def == 25);
    CHECK(s[Sync].def == 1); CHECK(s[PingPong].def == 0);
}
TEST_CASE("DL01 reports no delay; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("DL01 the echo comes back after Time, and Feedback repeats it") {
    for (double ms : {10.0, 100.0, 375.0, 1500.0}) {
        auto p = make(plain(ms, 35));
        const auto y = run(p, impulse(4.0));
        const size_t d = static_cast<size_t>(std::lround(ms * 0.001 * kFs));
        CHECK(peakAt(y, 100, 2 * d - 100) == d);
        NEAR(peakAbs(y, d - 1, d + 2), 0.98, 0.03);
        if (2 * d + 2 < y.size()) NEAR(peakAbs(y, 2 * d - 1, 2 * d + 2) / peakAbs(y, d - 1, d + 2), 0.35, 0.02);
    }
}
TEST_CASE("DL01 Feedback 110 % settles instead of running away") {
    for (int mode = 0; mode < 3; ++mode) {
        Set s = plain(120, 110); s.push_back({Mode, double(mode)}); s.push_back({Hpf, 100}); s.push_back({Lpf, 8000});
        auto p = make(s);
        auto x = noise(-12, 0.3, 4); x.resize(static_cast<size_t>(20 * kFs), 0.0f);
        const auto y = run(p, x);
        double m = 0; for (float v : y) { CHECK(std::isfinite(v)); m = std::max(m, double(std::abs(v))); }
        CHECK(m < 8.0);
        CHECK(m > 0.05);   // it rings on (> 100 % does not die out within 20 s)
        CHECK(peakAbs(y, y.size() - 48000, y.size()) > 0.05);
    }
}
TEST_CASE("DL01 loop filters: LPF takes the highs off the repeats, HPF the lows") {
    auto tone = [](double f) { auto x = sine(-12, 0.1, f); x.resize(static_cast<size_t>(3 * kFs), 0.0f); return x; };
    auto repeats = [&](double f, Set s) { auto p = make(s); const auto y = run(p, tone(f)); const size_t d = 24000; return rmsDb(y, 3 * d + 2000, 3 * d + 4000) - rmsDb(y, d + 2000, d + 4000); };
    Set lp = plain(500, 60); lp.push_back({Lpf, 2000});
    Set none = plain(500, 60);
    CHECK(repeats(8000, lp) < repeats(8000, none) - 12.0);   // 8 kHz: far more loss per round with the LPF at 2 kHz
    NEAR(repeats(300, lp), repeats(300, none), 1.0);          // 300 Hz is untouched
    Set hp = plain(500, 60); hp.push_back({Hpf, 800});
    CHECK(repeats(100, hp) < repeats(100, none) - 12.0);
    NEAR(repeats(5000, hp), repeats(5000, none), 1.0);
}
TEST_CASE("DL01 modes: Tape saturates, Analog loses the highs, Digital is clean") {
    auto burst = [](double f, double db) { auto x = sine(db, 0.2, f); x.resize(static_cast<size_t>(1.2 * kFs), 0.0f); return x; };
    auto echoOf = [&](int mode, double f, double db, double fb) { Set s = plain(400, fb); s.push_back({Mode, double(mode)}); auto p = make(s); return run(p, burst(f, db)); };
    auto thd = [](const std::vector<float>& y) { const size_t a = 19200 + 2400, b = 19200 + 7200; return binDb(y, 3000, a, b) - binDb(y, 1000, a, b); };
    CHECK(thd(echoOf(Digital, 1000, 0, 0)) < -40.0);
    CHECK(thd(echoOf(Tape, 1000, 0, 0)) > -30.0);
    const size_t a = 19200 + 2400, b = 19200 + 7200;
    const double hiDig = binDb(echoOf(Digital, 8000, -12, 0), 8000, a, b), hiAn = binDb(echoOf(Analog, 8000, -12, 0), 8000, a, b), hiTape = binDb(echoOf(Tape, 8000, -12, 0), 8000, a, b);
    CHECK(hiAn < hiDig - 6.0);
    CHECK(hiTape > hiAn + 3.0); CHECK(hiTape < hiDig - 0.5);
}
TEST_CASE("DL01 Depth and Rate move the pitch of the echo") {
    auto x = sine(-12, 2.0, 1000);
    Set a = plain(300, 0); a.push_back({Mode, Tape}); a.push_back({Depth, 100}); a.push_back({Rate, 3.0});
    Set b = plain(300, 0); b.push_back({Mode, Tape}); b.push_back({Depth, 0});
    auto pa = make(a); auto pb = make(b);
    const auto ya = run(pa, x), yb = run(pb, x);
    const double carrierA = binDb(ya, 1000, 24000, 96000) - rmsDb(ya, 24000, 96000), carrierB = binDb(yb, 1000, 24000, 96000) - rmsDb(yb, 24000, 96000);
    CHECK(carrierA < carrierB - 3.0);   // the energy leaves the carrier for the sidebands
    // Depth 0: no movement at all (the same as the plain delay)
    auto pc = make(plain(300, 0)); const auto yc = run(pc, x); CHECK(peakAbs(yc, 24000, 25000) > 0.2);
}
TEST_CASE("DL01 Ping-pong: the first echo is left, the second right") {
    Set s = plain(200, 60); s.push_back({PingPong, 1});
    auto p = make(s); auto l = impulse(1.0); auto r = std::vector<float>(l.size(), 0.0f);
    const auto o = run2(p, l, l);
    CHECK(peakAbs(o.first, 9500, 9700) > 0.5); CHECK(peakAbs(o.second, 9500, 9700) < 1e-6);
    CHECK(peakAbs(o.second, 19100, 19300) > 0.3); CHECK(peakAbs(o.first, 19100, 19300) < 1e-6);
    CHECK(peakAbs(o.first, 28700, 28900) > 0.15);
    // Off: both channels echo together
    auto q = make(plain(200, 60)); const auto n = run2(q, l, l);
    CHECK(peakAbs(n.first, 9500, 9700) > 0.5); CHECK(peakAbs(n.second, 9500, 9700) > 0.5);
    (void)r;
}
TEST_CASE("DL01 Sync: the knob picks a note at the host tempo") {
    CHECK(kNumNotes == 18);
    NEAR(noteQuarters(0), 1.0 / 16.0, 1e-12); NEAR(noteQuarters(17), 8.0, 1e-12);
    for (int i = 1; i < kNumNotes; ++i) CHECK(noteQuarters(i) > noteQuarters(i - 1));
    NEAR(noteSeconds(11, 120), 0.5, 1e-12);
    // the knob at the left end: 1/64; at the right end: 2 bars (4 s at 120 bpm)
    for (int bpm : {90, 120, 140}) {
        Set s = plain(1, 0); s.push_back({Sync, 1}); auto lo = make(s, bpm); NEAR(lo.timeSeconds(), noteSeconds(0, bpm), 1e-9);
        s.push_back({Time, 2000}); auto hi = make(s, bpm); NEAR(hi.timeSeconds(), std::min(noteSeconds(17, bpm), kMaxSeconds), 1e-9);
    }
    // without a tempo, Time is milliseconds; with Sync off it is too
    Set s = plain(375, 0); s.push_back({Sync, 1}); auto none = make(s); NEAR(none.timeSeconds(), 0.375, 1e-9);
    auto off = make(plain(375, 0), 120); NEAR(off.timeSeconds(), 0.375, 1e-9);
    // the echo really lands on the note
    Set q = plain(375, 0); q.push_back({Sync, 1}); auto p = make(q, 120);
    const size_t d = static_cast<size_t>(std::lround(p.timeSeconds() * kFs));
    const auto y = run(p, impulse(4.5)); CHECK(peakAt(y, 100, y.size()) == d);
}
TEST_CASE("DL01 Duck lowers the echo while the input is loud") {
    auto x = noise(-10, 4.0, 6);
    Set a = plain(150, 40); a.push_back({Duck, 12}); auto pa = make(a); auto pb = make(plain(150, 40));
    const auto ya = run(pa, x), yb = run(pb, x);
    NEAR(rmsDb(ya, 96000, 192000) - rmsDb(yb, 96000, 192000), -12.0, 1.5);
    // in the gaps the echo is back at full level
    auto g = noise(-10, 0.5, 6); g.resize(static_cast<size_t>(3 * kFs), 0.0f);
    auto pc = make(a); auto pd = make(plain(150, 40));
    const auto yc = run(pc, g), yd = run(pd, g);
    NEAR(rmsDb(yc, 2 * 48000, 2 * 48000 + 2400) , rmsDb(yd, 2 * 48000, 2 * 48000 + 2400), 0.3);
    (void)yc;
}
