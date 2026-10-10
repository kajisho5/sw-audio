// SW IN07 SWINGBY: the planets (2026-10-10, the client's "7以外ぜんぶ！"; README「IN07 の惑星の機能」). Appended after the gate steps:
//   planetary alignment (the arp: each held key on its own orbit, the highest the fastest; all of them together every so many steps),
//   the eclipse gate (a run of closed steps is one passage of the moon over the sun: a smooth dip, total at its middle),
//   satellite unison (the unison copies orbit: their detune and pan go round at a rate),
//   the Roche limit (a note harder than the limit tears its copies apart in pitch and pan; they fall back together over a time).
#include "doctest.h"
#include "in07/in07.hpp"
#include "in07/presets.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace sw::in07;

namespace {
constexpr double kFs = 48000.0;
constexpr size_t kStep = 6000;   // a 1/16 at 120 bpm

int idOf(const std::string& id) {
    const auto& s = specs();
    for (int i = 0; i < kNumParams; ++i) if (id == s[static_cast<size_t>(i)].id) return i;
    return -1;
}
void simple(Processor& p) {   // one sine layer, a flat envelope, no effects
    applyInit(p);
    p.setParam(lp(0, Wave), Sine);
    p.setParam(lp(0, AmpA), 0.5); p.setParam(lp(0, AmpD), 1); p.setParam(lp(0, AmpS), 100); p.setParam(lp(0, AmpR), 1);
    for (int f = 0; f < kFx; ++f) p.setParam(fxOnId(f), 0);
}
// when each key's arp note starts (keyHeld: a key the arp plays), one sample at a time
std::map<int, std::vector<size_t>> starts(Processor& p, size_t n) {
    std::map<int, std::vector<size_t>> on;
    std::vector<bool> was(128, false);
    float l = 0, r = 0;
    float* c[2] = {&l, &r};
    for (size_t t = 0; t < n; ++t) {
        p.process(c, 2, 1);
        for (int k = 0; k < 128; ++k) {
            const bool h = p.keyHeld(k);
            if (h && !was[static_cast<size_t>(k)]) on[k].push_back(t);
            was[static_cast<size_t>(k)] = h;
        }
    }
    return on;
}
void render(Processor& p, size_t n, std::vector<float>& L, std::vector<float>& R) {
    L.assign(n, 0.0f); R.assign(n, 0.0f);
    for (size_t off = 0; off < n; off += 256) {
        const int k = static_cast<int>(std::min<size_t>(256, n - off));
        float* c[2] = {L.data() + off, R.data() + off};
        p.process(c, 2, k);
    }
}
double rms(const std::vector<float>& x, size_t a, size_t b) {
    double s = 0.0;
    for (size_t i = a; i < b; ++i) s += static_cast<double>(x[i]) * x[i];
    return std::sqrt(s / static_cast<double>(std::max<size_t>(1, b - a)));
}
// the frequency over [a, b) from the rising zero crossings (interpolated)
double freqOf(const std::vector<float>& x, size_t a, size_t b) {
    std::vector<double> z;
    for (size_t i = a + 1; i < b; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) z.push_back(static_cast<double>(i - 1) + x[i - 1] / (x[i - 1] - x[i]));
    if (z.size() < 2) return 0.0;
    return (z.size() - 1) * kFs / (z.back() - z.front());
}
}  // namespace

TEST_CASE("IN07 PLANETS: the parameters (appended after the gate steps; all of them off by default)") {
    const auto& s = specs();
    CHECK(ArpAlign == gateStep(kArpSteps - 1) + 1);
    CHECK(idOf("in07.arp.align") == ArpAlign);
    CHECK(idOf("in07.gate.shape") == GateShape);
    CHECK(idOf("in07.sat.rate") == SatRate);
    CHECK(idOf("in07.sat.depth") == SatDepth);
    CHECK(idOf("in07.roche.limit") == RocheLimit);
    CHECK(idOf("in07.roche.spread") == RocheSpread);
    CHECK(idOf("in07.roche.time") == RocheTime);
    const auto& al = s[static_cast<size_t>(ArpAlign)];
    REQUIRE(al.labels.size() == 4);
    CHECK(al.labels[0] == "Off"); CHECK(al.labels[1] == "2-3-4"); CHECK(al.labels[2] == "3-4-5"); CHECK(al.labels[3] == "3-5-7");
    CHECK(al.def == AlignOff);
    const auto& gs = s[static_cast<size_t>(GateShape)];
    REQUIRE(gs.labels.size() == 2);
    CHECK(gs.labels[0] == "Hard"); CHECK(gs.labels[1] == "Eclipse");
    CHECK(gs.def == GateHard);
    CHECK(s[static_cast<size_t>(SatRate)].min == doctest::Approx(0.05)); CHECK(s[static_cast<size_t>(SatRate)].max == doctest::Approx(10.0));
    CHECK(s[static_cast<size_t>(SatRate)].def == doctest::Approx(0.5)); CHECK(s[static_cast<size_t>(SatRate)].curve == sw::Curve::Log);
    CHECK(s[static_cast<size_t>(SatDepth)].def == 0.0); CHECK(s[static_cast<size_t>(SatDepth)].max == 100.0);
    CHECK(s[static_cast<size_t>(RocheLimit)].def == 100.0);   // 100 % = off (no velocity is over it)
    CHECK(s[static_cast<size_t>(RocheLimit)].maxLabel == std::string("Off"));
    CHECK(s[static_cast<size_t>(RocheSpread)].max == 24.0); CHECK(s[static_cast<size_t>(RocheSpread)].def == 7.0);
    CHECK(s[static_cast<size_t>(RocheTime)].min == 20.0); CHECK(s[static_cast<size_t>(RocheTime)].max == 2000.0);
    CHECK(s[static_cast<size_t>(RocheTime)].def == 300.0); CHECK(s[static_cast<size_t>(RocheTime)].curve == sw::Curve::Log);
    for (int i = ArpAlign; i <= RocheTime; ++i) CHECK_MESSAGE(s[static_cast<size_t>(i)].automatable, s[static_cast<size_t>(i)].id);
    // no factory preset uses them yet: every preset leaves them at the defaults
    for (int i = 0; i < static_cast<int>(factoryPresets().size()); ++i) {
        std::vector<double> v; presetValues(i, v);
        for (int id = ArpAlign; id <= RocheTime; ++id) CHECK(v[static_cast<size_t>(id)] == s[static_cast<size_t>(id)].def);
    }
}

TEST_CASE("IN07 ALIGN: each held key on its own orbit (the highest the fastest); all of them together at the start and every 12 steps") {
    Processor p;
    simple(p);
    p.setParam(ArpOn, 1); p.setParam(ArpRate, 1); p.setParam(ArpLength, 50); p.setParam(ArpAlign, Align234);
    p.prepare(kFs, 256); p.setTempo(120);
    for (int k : {60, 64, 67}) p.noteOn(k, 0.8);
    auto on = starts(p, 24 * kStep - 100);   // steps 0 .. 23 (the clock starts with the first key)
    REQUIRE(on[67].size() >= 2);
    const size_t t0 = on[67][0];
    // C4 E4 G4: G (the highest, the innermost) every 2 steps, E every 3, C every 4
    CHECK(on[67].size() == 12); CHECK(on[64].size() == 8); CHECK(on[60].size() == 6);
    for (size_t j = 0; j < on[67].size(); ++j) CHECK(on[67][j] == t0 + 2 * j * kStep);
    for (size_t j = 0; j < on[64].size(); ++j) CHECK(on[64][j] == t0 + 3 * j * kStep);
    for (size_t j = 0; j < on[60].size(); ++j) CHECK(on[60][j] == t0 + 4 * j * kStep);
    // nothing else plays (no octaves, no other keys)
    for (const auto& e : on) CHECK_MESSAGE((e.first == 60 || e.first == 64 || e.first == 67), e.first);
    // 3-5-7 with four keys: 3, 5, 7 and then 9 steps
    Processor q;
    simple(q);
    q.setParam(ArpOn, 1); q.setParam(ArpRate, 1); q.setParam(ArpLength, 50); q.setParam(ArpAlign, Align357);
    q.prepare(kFs, 256); q.setTempo(120);
    for (int k : {48, 55, 60, 64}) q.noteOn(k, 0.8);
    auto o2 = starts(q, 45 * kStep - 100);
    REQUIRE(o2[64].size() >= 2);
    const size_t u0 = o2[64][0];
    CHECK(o2[64].size() == 15); CHECK(o2[60].size() == 9); CHECK(o2[55].size() == 7); CHECK(o2[48].size() == 5);
    CHECK(o2[48][1] == u0 + 9 * kStep);
    CHECK(o2[55][1] == u0 + 7 * kStep);
    // a rest on the velocity row silences every key on that step; the pitch row moves all of them
    Processor r;
    simple(r);
    r.setParam(ArpOn, 1); r.setParam(ArpRate, 1); r.setParam(ArpLength, 50); r.setParam(ArpAlign, Align234);
    r.setParam(arpVel(0), 0); r.setParam(arpPitch(2), 12);
    r.prepare(kFs, 256); r.setTempo(120);
    for (int k : {60, 64, 67}) r.noteOn(k, 0.8);
    auto o3 = starts(r, 16 * kStep - 100);
    CHECK(o3[60].size() == 3);   // steps 4, 8, 12 (0 is a rest)
    CHECK(o3[79].size() == 1);   // step 2: G up an octave
    // Off: the arp as before (Up, one key a step)
    Processor u;
    simple(u);
    u.setParam(ArpOn, 1); u.setParam(ArpRate, 1); u.setParam(ArpLength, 50);
    u.prepare(kFs, 256); u.setTempo(120);
    for (int k : {60, 64, 67}) u.noteOn(k, 0.8);
    auto o4 = starts(u, 6 * kStep - 100);
    CHECK(o4[60].size() == 2); CHECK(o4[64].size() == 2); CHECK(o4[67].size() == 2);
}

TEST_CASE("IN07 ECLIPSE: a run of closed steps is one passage of the moon: the sound dims smoothly, out at the middle, back by its end") {
    auto run = [](int shape) {
        Processor p;
        simple(p);
        p.setParam(GateOn, 1); p.setParam(GateRate, 1); p.setParam(GateDepth, 100); p.setParam(GateShape, shape);
        for (int i = 0; i < kArpSteps; ++i) p.setParam(gateStep(i), (i >= 4 && i < 8) || i == 9 ? 0 : 1);   // closed: 4-7 and 9
        p.prepare(kFs, 256); p.setTempo(120);
        p.noteOn(60, 0.8);
        std::vector<float> L, R;
        render(p, 16 * kStep, L, R);
        return L;
    };
    const auto e = run(GateEclipse), h = run(GateHard);
    const double full = rms(e, kStep, 2 * kStep);   // step 1: open
    auto at = [&](const std::vector<float>& x, double step) { const size_t c = static_cast<size_t>(step * kStep); return rms(x, c - 120, c + 120) / full; };
    // the four-step run: totality at its middle (step 6.0), half-covered a quarter of the way in, all but open at its edges
    CHECK(at(e, 6.0) < 0.01);
    CHECK(at(e, 5.0) > 0.25); CHECK(at(e, 5.0) < 0.85);
    CHECK(at(e, 4.05) > 0.9);
    CHECK(at(e, 7.95) > 0.9);
    CHECK(at(e, 4.5) > at(e, 5.0)); CHECK(at(e, 5.0) > at(e, 5.5));   // dimming
    CHECK(at(e, 6.5) < at(e, 7.0)); CHECK(at(e, 7.0) < at(e, 7.5));   // and back
    // one closed step: a short eclipse, total in its middle
    CHECK(at(e, 9.5) < 0.01);
    CHECK(at(e, 9.1) > 0.4);
    // the hard gate is shut through the run
    CHECK(at(h, 5.0) < 0.01); CHECK(at(h, 4.5) < 0.01);
    // smooth: no sample-to-sample jump larger than the open sine's own
    double open = 0.0, step = 0.0;
    for (size_t i = kStep + 1; i < 2 * kStep; ++i) open = std::max(open, std::abs(static_cast<double>(e[i]) - e[i - 1]));
    for (size_t i = 4 * kStep - 600; i < 10 * kStep + 600; ++i) step = std::max(step, std::abs(static_cast<double>(e[i]) - e[i - 1]));
    CHECK(step <= open * 1.01);
    // every step closed: one eclipse over the whole cycle
    Processor p;
    simple(p);
    p.setParam(GateOn, 1); p.setParam(GateRate, 1); p.setParam(GateDepth, 100); p.setParam(GateShape, GateEclipse);
    for (int i = 0; i < kArpSteps; ++i) p.setParam(gateStep(i), 0);
    p.prepare(kFs, 256); p.setTempo(120);
    p.noteOn(60, 0.8);
    std::vector<float> L, R;
    render(p, 16 * kStep, L, R);
    CHECK(rms(L, 8 * kStep - 240, 8 * kStep + 240) < 0.01 * full);
    CHECK(rms(L, 1 * kStep - 240, 1 * kStep + 240) > 0.5 * full);
}

TEST_CASE("IN07 SATELLITES: the unison copies orbit: detune and pan go round at the rate; depth 0 changes nothing") {
    // one copy, a sine: it circles the centre (the pan swings, the pitch swings by the detune), 2 Hz
    Processor p;
    simple(p);
    p.setParam(lp(0, Unison), 1); p.setParam(lp(0, Detune), 100); p.setParam(lp(0, Spread), 100);
    p.setParam(SatRate, 2.0); p.setParam(SatDepth, 100);
    p.prepare(kFs, 256);
    p.noteOn(69, 0.8);
    std::vector<float> L, R;
    render(p, static_cast<size_t>(2.0 * kFs), L, R);
    // the balance (L - R) / (L + R) in 25 ms windows: it swings from side to side, twice a second
    std::vector<double> bal;
    const size_t w = static_cast<size_t>(0.025 * kFs);
    for (size_t a = static_cast<size_t>(0.25 * kFs); a + w <= L.size(); a += w) { const double l = rms(L, a, a + w), r = rms(R, a, a + w); bal.push_back((l - r) / (l + r + 1e-12)); }
    const auto mm = std::minmax_element(bal.begin(), bal.end());
    CHECK(*mm.first < -0.5); CHECK(*mm.second > 0.5);
    int flips = 0;
    for (size_t i = 1; i < bal.size(); ++i) flips += (bal[i - 1] < 0) != (bal[i] < 0);
    CHECK(flips >= 6); CHECK(flips <= 8);   // 1.75 s at 2 Hz: 7 sign changes
    // the pitch: the detune at its widest (Detune 100 % = 50 cents) one side, then the other
    double lo = 1e9, hi = 0;
    for (size_t a = static_cast<size_t>(0.25 * kFs); a + 2400 <= L.size(); a += 1200) { const double f = freqOf(L, a, a + 2400); if (f > 0) { lo = std::min(lo, f); hi = std::max(hi, f); } }
    CHECK(1200.0 * std::log2(hi / lo) > 80.0);
    // depth 0: the rate changes nothing (the same samples)
    auto plain = [](double rate) {
        Processor q;
        simple(q);
        q.setParam(lp(0, Wave), Saw); q.setParam(lp(0, Unison), 4); q.setParam(lp(0, Detune), 40); q.setParam(lp(0, Spread), 80);
        q.setParam(SatRate, rate); q.setParam(SatDepth, 0);
        q.prepare(kFs, 256);
        q.noteOn(57, 0.8);
        std::vector<float> l, r;
        render(q, 24000, l, r);
        return l;
    };
    CHECK(plain(0.3) == plain(7.0));
    // two copies, depth 100: opposite each other on the orbit, so they trade places: while one is on the left and lower, the other is on
    // the right and higher, then the other way round. The left channel's pitch against the right's changes sign four times a cycle (where
    // the two pitches meet, and where the copies cross the middle)
    Processor s2;
    simple(s2);
    s2.setParam(lp(0, Unison), 2); s2.setParam(lp(0, Detune), 100); s2.setParam(lp(0, Spread), 100);
    s2.setParam(SatRate, 1.0); s2.setParam(SatDepth, 100);
    s2.prepare(kFs, 256);
    s2.noteOn(69, 0.8);
    render(s2, static_cast<size_t>(2.25 * kFs), L, R);
    std::vector<double> dc;
    for (size_t a = static_cast<size_t>(0.25 * kFs); a + 2400 <= L.size(); a += 1200) {
        const double fl = freqOf(L, a, a + 2400), fr = freqOf(R, a, a + 2400);
        if (fl > 0 && fr > 0) dc.push_back(1200.0 * std::log2(fl / fr));
    }
    int changes = 0; double widest = 0;
    for (size_t i = 1; i < dc.size(); ++i) changes += (dc[i - 1] < 0) != (dc[i] < 0);
    for (double x : dc) widest = std::max(widest, std::abs(x));
    CHECK(changes >= 6); CHECK(changes <= 9);   // 1.95 s at 1 Hz: about 8
    CHECK(widest > 30.0);
}

TEST_CASE("IN07 ROCHE: a note harder than the limit is torn apart in pitch, and falls back together over the time") {
    auto run = [](double vel, double limit, int unison) {
        Processor p;
        simple(p);
        p.setParam(lp(0, Unison), unison); p.setParam(lp(0, Detune), 0); p.setParam(lp(0, Spread), 0);
        p.setParam(RocheLimit, limit); p.setParam(RocheSpread, 12); p.setParam(RocheTime, 100);
        p.prepare(kFs, 256);
        p.noteOn(69, vel);
        std::vector<float> L, R;
        render(p, static_cast<size_t>(1.2 * kFs), L, R);
        return L;
    };
    const double a440 = 440.0;
    // velocity 1 over a limit of 50 %: torn the whole way (12 semitones off at first), back within 0.15 semitone after 6 time constants
    const auto t = run(1.0, 50, 1);
    const double f0 = freqOf(t, 0, 480), f1 = freqOf(t, static_cast<size_t>(0.8 * kFs), static_cast<size_t>(1.0 * kFs));
    CHECK(std::abs(12.0 * std::log2(f0 / a440)) > 8.0);
    CHECK(std::abs(12.0 * std::log2(f1 / a440)) < 0.15);
    // under the limit: untouched (the note's own pitch from the start)
    const auto u = run(0.4, 50, 1);
    CHECK(std::abs(12.0 * std::log2(freqOf(u, 0, 960) / a440)) < 0.1);
    // the limit at 100 %: off (no velocity is over it)
    const auto off = run(1.0, 100, 1);
    CHECK(std::abs(12.0 * std::log2(freqOf(off, 0, 960) / a440)) < 0.1);
    // halfway over the limit: half the spread
    const auto half = run(0.75, 50, 1);
    const double h0 = std::abs(12.0 * std::log2(freqOf(half, 0, 480) / a440));
    CHECK(h0 > 3.5); CHECK(h0 < 6.5);
    // four copies: thrown apart (a wide spread of pitches at first: the waveform is not periodic), one pitch later
    const auto f4 = run(1.0, 0, 4);
    CHECK(std::abs(12.0 * std::log2(freqOf(f4, static_cast<size_t>(0.8 * kFs), static_cast<size_t>(1.0 * kFs)) / a440)) < 0.15);
}
