// SW IN07 engine prototype: parameter table, oscillator band-limiting, filter shapes, envelopes, sleep, glide, unison, drive, robustness
#include "doctest.h"
#include "in07/in07.hpp"
#include "sw/fft.hpp"
#include "tu.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <set>
#include <string>
#include <vector>

using namespace sw::in07;

namespace {
const double kFs = tu::kFs;

// a processor with a plain test patch: sine, 1 voice, filter wide open, no envelopes on the filter, sustain 100 %
void plain(Processor& p) {
    p.prepare(kFs, 256);
    p.setParam(Wave, Sine); p.setParam(Unison, 1); p.setParam(Octave, 0);
    p.setParam(FilterType, LP12); p.setParam(Cutoff, 20000); p.setParam(Resonance, 0); p.setParam(Drive, 0);
    p.setParam(FilterEnv, 0); p.setParam(KeyTrack, 0); p.setParam(VelSens, 0); p.setParam(Glide, 0); p.setParam(Level, 0);
    p.setParam(AmpA, 0.5); p.setParam(AmpD, 1); p.setParam(AmpS, 100); p.setParam(AmpR, 10);   // ms
}
// render n samples (blocks of at most `block`); returns left and right
std::pair<std::vector<float>, std::vector<float>> render(Processor& p, size_t n, int block = 256) {
    std::vector<float> l(n), r(n);
    for (size_t off = 0; off < n; off += static_cast<size_t>(block)) {
        const int m = static_cast<int>(std::min<size_t>(static_cast<size_t>(block), n - off));
        float* c[2] = {l.data() + off, r.data() + off};
        p.process(c, 2, m);
    }
    return {l, r};
}
double midiHz(int note) { return 440.0 * std::exp2((note - 69) / 12.0); }
// the steady level (dB) of a held note with this patch
double heldDb(Processor& p, int note) {
    p.noteOn(note, 1.0);
    auto y = render(p, 24000).first;
    p.allNotesOff(); render(p, 4800);
    return tu::rmsDb(y, 12000, 24000);
}
// aliasing of a periodic oscillator: power off the harmonic bins over the power of the fundamental (dB)
double aliasDb(bool correct, int wave) {
    const int n = 65536, k = 2543;     // f0 = 48000 * 2543 / 65536 = 1862.5 Hz; 2543 is prime: no alias lands on a harmonic bin
    BlepOsc o; o.setWave(wave); o.setIncrement(static_cast<double>(k) / n); o.setCorrection(correct);
    std::vector<std::complex<double>> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) o.next();               // one period of the window first: minBLEP carries 64 samples of past jumps
    for (auto& v : x) v = o.next();
    sw::Fft f(n); f.forward(x);
    double fund = 0, alias = 0;
    for (int b = 1; b < n / 2; ++b) {
        const double pw = std::norm(x[static_cast<size_t>(b)]);
        if (b % k == 0) { if (b == k) fund = pw; } else alias += pw;
    }
    return 10 * std::log10(alias / fund);
}
std::vector<float> oscRun(int wave, double hz, double pw, size_t n) {
    BlepOsc o; o.setWave(wave); o.setPulseWidth(pw); o.setIncrement(hz / kFs);
    std::vector<float> y(n); for (auto& v : y) v = static_cast<float>(o.next());
    return y;
}
}  // namespace

TEST_CASE("IN07: parameter table") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    std::set<std::string> ids;
    for (const auto& p : s) {
        ids.insert(p.id);
        CHECK(std::string(p.id).rfind("in07.", 0) == 0);
        CHECK(p.def >= std::min(p.min, p.max));
        CHECK(p.def <= std::max(p.min, p.max));
    }
    CHECK(ids.size() == s.size());
    CHECK(std::string(s[Cutoff].id) == "in07.flt.cutoff");
    CHECK(s[Cutoff].curve == sw::Curve::Log);
    CHECK(s[Cutoff].def == doctest::Approx(2400));
    CHECK(s[AmpA].curve == sw::Curve::Log);
    CHECK(s[AmpR].curve == sw::Curve::Log);
    CHECK(s[Wave].numSteps() == 4);
    CHECK(s[FilterType].numSteps() == 4);
    CHECK(s[Unison].numSteps() == 8);
}

TEST_CASE("IN07: minBLEP saw and square alias far less than the naive waveforms") {
    const double sawBlep = aliasDb(true, Saw), sawNaive = aliasDb(false, Saw);
    const double sqBlep = aliasDb(true, Square), sqNaive = aliasDb(false, Square);
    const double triBlamp = aliasDb(true, Triangle), triNaive = aliasDb(false, Triangle);
    MESSAGE("alias at 1862.5 Hz: saw naive " << sawNaive << " / minBLEP " << sawBlep << " dB; square " << sqNaive << " / " << sqBlep
            << " dB; triangle naive " << triNaive << " / polyBLAMP " << triBlamp << " dB");
    CHECK(sawBlep < -80.0);              // measured -97.8 dB (naive -11.0 dB, 2-point polyBLEP -26.3 dB)
    CHECK(sawNaive - sawBlep > 60.0);
    CHECK(sqBlep < -80.0);               // measured -101.6 dB
    CHECK(sqNaive - sqBlep > 60.0);
    CHECK(triBlamp < triNaive - 10.0);
}

TEST_CASE("IN07: no DC offset at any pitch") {
    for (double f : {100.0, 1000.0, 5000.0, 9000.0})
        for (int wave : {int(Saw), int(Square), int(Square) + 10}) {   // Square + 10: a 35 % pulse
            BlepOsc o; o.setWave(wave > Square ? Square : wave); o.setPulseWidth(wave > Square ? 0.35 : 0.5); o.setIncrement(f / kFs);
            for (int i = 0; i < 4800; ++i) o.next();
            double s = 0; const int n = 480000;
            for (int i = 0; i < n; ++i) s += o.next();
            CHECK(std::abs(s / n) < 1e-3);
        }
}

TEST_CASE("IN07: the saw keeps its harmonics up to 18 kHz") {
    const int n = 65536, k = 2543;
    BlepOsc o; o.setWave(Saw); o.setIncrement(static_cast<double>(k) / n);
    std::vector<std::complex<double>> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) o.next();
    for (auto& v : x) v = o.next();
    sw::Fft f(n); f.forward(x);
    const double f1 = std::abs(x[static_cast<size_t>(k)]);
    for (int h = 2; h <= 10; ++h)        // harmonic h of a saw: 1/h (10 x 1862.5 Hz = 18.6 kHz)
        CHECK(20 * std::log10(std::abs(x[static_cast<size_t>(k * h)]) / f1 * h) == doctest::Approx(0.0).epsilon(0.3));
}

TEST_CASE("IN07: triangle and pulse harmonics") {
    const size_t n = 48000;
    auto tri = oscRun(Triangle, 220, 0.5, n);
    CHECK(tu::harmDb(tri, 220, 3) == doctest::Approx(20 * std::log10(1.0 / 9.0)).epsilon(0.03));
    CHECK(tu::harmDb(tri, 220, 2) < -60.0);
    auto sq = oscRun(Square, 220, 0.5, n);
    CHECK(tu::harmDb(sq, 220, 3) == doctest::Approx(20 * std::log10(1.0 / 3.0)).epsilon(0.03));
    CHECK(tu::harmDb(sq, 220, 2) < -60.0);
    auto pulse = oscRun(Square, 220, 0.25, n);   // harmonic h of a pulse: |sin(h pi d)| / h -> 2nd / 1st = 1 / (2 sin(pi / 4))
    CHECK(tu::harmDb(pulse, 220, 2) == doctest::Approx(20 * std::log10(1.0 / (2.0 * std::sin(tu::kPi / 4)))).epsilon(0.05));
}

TEST_CASE("IN07: pitch and octave") {
    Processor p; plain(p);
    p.noteOn(69, 1.0);
    auto y = render(p, 48000).first;
    CHECK(tu::binDb(y, 440, 24000, 48000) > tu::binDb(y, 430, 24000, 48000) + 30);
    CHECK(tu::binDb(y, 440, 24000, 48000) > tu::binDb(y, 450, 24000, 48000) + 30);
    p.allNotesOff(); render(p, 4800);
    p.setParam(Octave, -1);
    p.noteOn(69, 1.0);
    y = render(p, 48000).first;
    CHECK(tu::binDb(y, 220, 24000, 48000) > tu::binDb(y, 440, 24000, 48000) + 40);
}

TEST_CASE("IN07: LP24 at Res 0 is a 4th-order Butterworth") {
    Processor p; plain(p); p.setParam(FilterType, LP24);
    const double open = heldDb(p, 83);                 // 987.8 Hz, cutoff 20 kHz
    p.setParam(Cutoff, midiHz(83));
    CHECK(heldDb(p, 83) - open == doctest::Approx(-3.01).epsilon(0.03));
    p.setParam(Cutoff, 20000);
    const double open71 = heldDb(p, 71);               // 493.9 Hz
    p.setParam(Cutoff, midiHz(71) / 2);                // one octave below the note: 10 log10(1 + 2^8) = 24.1 dB down
    CHECK(heldDb(p, 71) - open71 == doctest::Approx(-24.1).epsilon(0.015));
}

TEST_CASE("IN07: resonance, HP12 and BP12") {
    Processor p; plain(p);
    const double open = heldDb(p, 81);                 // 880 Hz
    p.setParam(Cutoff, 880);
    CHECK(heldDb(p, 81) - open == doctest::Approx(-3.01).epsilon(0.03));
    p.setParam(Resonance, 100);
    CHECK(heldDb(p, 81) - open > 20.0);
    p.setParam(Resonance, 0); p.setParam(FilterType, HP12);
    CHECK(heldDb(p, 81) - open == doctest::Approx(-3.01).epsilon(0.03));
    CHECK(heldDb(p, 69) - open < -11.0);               // an octave below a 2nd-order high-pass: about -12.3 dB
    p.setParam(FilterType, BP12);
    CHECK(heldDb(p, 81) - open == doctest::Approx(0.0).epsilon(0.02));
    CHECK(heldDb(p, 105) - open < -5.0);              // two octaves above the centre: about -9 dB
}

TEST_CASE("IN07: filter envelope and key tracking move the cutoff") {
    Processor p; plain(p);
    p.setParam(Cutoff, 200); p.setParam(FilterEnv, 100);
    p.setParam(FenvA, 0.5); p.setParam(FenvD, 5000); p.setParam(FenvS, 100); p.setParam(FenvR, 1000);
    p.noteOn(60, 1.0); render(p, 4800);
    CHECK(p.voice().cutoffInUse() == doctest::Approx(200 * 32.0).epsilon(0.01));
    p.allNotesOff(); render(p, 96000);
    p.setParam(FilterEnv, -100);
    p.noteOn(60, 1.0); render(p, 4800);
    CHECK(p.voice().cutoffInUse() == doctest::Approx(20.0));
    p.allNotesOff(); render(p, 96000);
    p.setParam(FilterEnv, 0); p.setParam(KeyTrack, 100);
    p.noteOn(60, 1.0); render(p, 960);
    const double c60 = p.voice().cutoffInUse();
    p.allNotesOff(); render(p, 4800);
    p.noteOn(72, 1.0); render(p, 960);
    CHECK(p.voice().cutoffInUse() / c60 == doctest::Approx(2.0).epsilon(0.001));
}

TEST_CASE("IN07: amp envelope timing, sustain, release and sleep") {
    Processor p; plain(p);
    p.setParam(AmpA, 10); p.setParam(AmpD, 100); p.setParam(AmpS, 50); p.setParam(AmpR, 200);
    render(p, 64);                                     // the parameters reach the voice
    p.noteOn(60, 1.0);
    const auto& env = p.voice().ampEnv();
    int reached = -1;
    for (int i = 0; i < 2000 && reached < 0; ++i) { render(p, 1, 1); if (env.stage() != Adsr::Attack) reached = i + 1; }
    CHECK(reached == doctest::Approx(480).epsilon(0.01));
    render(p, 4800, 1);                                // the 100 ms decay
    CHECK(env.stage() == Adsr::Sustain);
    CHECK(env.level() == doctest::Approx(0.5));
    p.noteOff(60);
    int idle = -1;
    for (int i = 0; i < 20000 && idle < 0; ++i) { render(p, 1, 1); if (!p.active()) idle = i + 1; }
    CHECK(idle == doctest::Approx(9600).epsilon(0.01));
    auto y = render(p, 4800);
    CHECK(*std::max_element(y.first.begin(), y.first.end()) == 0.0f);
    CHECK(*std::min_element(y.first.begin(), y.first.end()) == 0.0f);
}

TEST_CASE("IN07: re-trigger continues from the current level") {
    Processor p; plain(p);
    p.setParam(AmpA, 5); p.setParam(AmpR, 500);
    p.noteOn(60, 1.0); render(p, 4800);
    p.noteOff(60); render(p, 4800);
    const auto& env = p.voice().ampEnv();
    double prev = env.level(), maxStep = 0;
    CHECK(prev > 0.1);
    p.noteOn(64, 1.0);
    for (int i = 0; i < 480; ++i) { render(p, 1, 1); maxStep = std::max(maxStep, std::abs(env.level() - prev)); prev = env.level(); }
    CHECK(maxStep < 0.02);
}

TEST_CASE("IN07: the block size does not change the output") {
    auto play = [](int block) {
        Processor p; plain(p);
        p.setParam(Wave, Saw); p.setParam(Unison, 4); p.setParam(Detune, 30); p.setParam(Spread, 80);
        p.setParam(FilterType, LP24); p.setParam(Cutoff, 900); p.setParam(Resonance, 40); p.setParam(FilterEnv, 50); p.setParam(Drive, 30);
        p.noteOn(57, 0.8);
        auto a = render(p, 9000, block);
        p.noteOff(57);
        auto b = render(p, 9000, block);
        a.first.insert(a.first.end(), b.first.begin(), b.first.end());
        return a.first;
    };
    const auto x = play(256), y = play(37);
    REQUIRE(x.size() == y.size());
    size_t diff = 0; for (size_t i = 0; i < x.size(); ++i) if (x[i] != y[i]) ++diff;
    CHECK(diff == 0);
}

TEST_CASE("IN07: glide moves linearly in semitones") {
    Processor p; plain(p); p.setParam(Glide, 200);
    p.noteOn(57, 1.0); render(p, 4800);
    CHECK(p.voice().frequency() == doctest::Approx(220.0).epsilon(0.001));
    p.noteOn(69, 1.0); render(p, 4800);                // half way: 100 ms of 200 ms
    CHECK(p.voice().frequency() == doctest::Approx(220.0 * std::sqrt(2.0)).epsilon(0.01));
    render(p, 9600);
    CHECK(p.voice().frequency() == doctest::Approx(440.0).epsilon(0.001));
}

TEST_CASE("IN07: unison spread and level") {
    auto play = [](int voices, double spread) {
        Processor p; plain(p);
        p.setParam(Wave, Saw); p.setParam(Unison, voices); p.setParam(Detune, 30); p.setParam(Spread, spread);
        p.noteOn(57, 1.0);
        return render(p, 48000);
    };
    auto mono = play(8, 0);
    size_t diff = 0; for (size_t i = 0; i < mono.first.size(); ++i) if (mono.first[i] != mono.second[i]) ++diff;
    CHECK(diff == 0);
    auto wide = play(8, 100);
    double sl = 0, sr = 0, slr = 0;
    for (size_t i = 24000; i < 48000; ++i) { sl += wide.first[i] * wide.first[i]; sr += wide.second[i] * wide.second[i]; slr += wide.first[i] * wide.second[i]; }
    CHECK(slr / std::sqrt(sl * sr) < 0.9);
    auto one = play(1, 0);
    CHECK(std::abs(tu::rmsDb(mono.first, 24000, 48000) - tu::rmsDb(one.first, 24000, 48000)) < 3.0);
}

TEST_CASE("IN07: Drive 0 is linear, Drive adds harmonics") {
    Processor p; plain(p);
    p.noteOn(57, 1.0);
    auto y = render(p, 48000).first;
    CHECK(tu::harmDb(y, 220, 3) < -90.0);
    p.allNotesOff(); render(p, 4800);
    p.setParam(Drive, 100);
    p.noteOn(57, 1.0);
    y = render(p, 48000).first;
    CHECK(tu::harmDb(y, 220, 3) > -40.0);
}

TEST_CASE("IN07: finite and bounded under random settings") {
    tu::Gauss g(7);
    Processor p; p.prepare(kFs, 256);
    const auto& s = specs();
    double peak = 0; bool finite = true;
    for (int round = 0; round < 40; ++round) {
        for (int id = 0; id < kNumParams; ++id) p.setParam(id, s[static_cast<size_t>(id)].toValue(g.uni()));
        p.setParam(AmpR, 20); p.setParam(Level, 0);
        p.noteOn(24 + static_cast<int>(g.uni() * 84), g.uni());
        auto y = render(p, 2400, 1 + static_cast<int>(g.uni() * 300));
        p.allNotesOff();
        auto z = render(p, 2400);
        for (float v : y.first) { finite = finite && std::isfinite(v); peak = std::max(peak, static_cast<double>(std::abs(v))); }
        for (float v : z.second) { finite = finite && std::isfinite(v); peak = std::max(peak, static_cast<double>(std::abs(v))); }
    }
    CHECK(finite);
    CHECK(peak < 100.0);
}
