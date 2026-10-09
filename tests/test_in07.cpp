// SW IN07 SWINGBY engine: parameter table, oscillator band-limiting, filter shapes, envelopes, sleep, glide, unison, drive,
// polyphony (voice limit, stealing, same-key re-trigger), four layers (level, pan, pitch offsets), bend, sustain pedal, mono / legato,
// note end reports, block-size independence, robustness
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

// L1 with a plain test patch: sine, 1 voice, filter wide open, no envelope on the filter, sustain 100 %; layers 2..4 off (their default)
void plain(Processor& p) {
    p.prepare(kFs, 256);
    p.setParam(Level, 0); p.setParam(Glide, 0);
    for (int f = 0; f < kFx; ++f) p.setParam(fxOnId(f), 0);   // the effects (on by default) stay out of the voice tests
    p.setParam(lp(0, Wave), Sine); p.setParam(lp(0, Unison), 1); p.setParam(lp(0, Octave), 0);
    p.setParam(lp(0, FilterType), LP12); p.setParam(lp(0, Cutoff), 20000); p.setParam(lp(0, Resonance), 0); p.setParam(lp(0, Drive), 0);
    p.setParam(lp(0, FilterEnv), 0); p.setParam(lp(0, KeyTrack), 0); p.setParam(lp(0, VelSens), 0);
    p.setParam(lp(0, AmpA), 0.5); p.setParam(lp(0, AmpD), 1); p.setParam(lp(0, AmpS), 100); p.setParam(lp(0, AmpR), 10);   // ms
}
// the same patch on layer l (turned on)
void plainLayer(Processor& p, int l) {
    p.setParam(lp(l, On), 1);
    for (int id = LayerLevel; id < kLayerParams; ++id) p.setParam(lp(l, id), p.param(lp(0, id)));
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
const Voice& v0(const Processor& p, int key, int layer = 0) {
    const Voice* v = p.find(key, layer);
    REQUIRE(v != nullptr);
    return *v;
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
    CHECK(PresetSelect == kNumGlobal + kLayers * kLayerParams + kFxParams + (kModSlotBase - kFxEnd) + kModSlots * kModFields);   // then the preset selector
    CHECK(kNumParams == PresetSelect + 1 + (7 + 2 * kArpSteps) + (3 + kArpSteps));   // then the arp (7 + 16 x 2) and the gate (3 + 16), appended 2026-10-09
    std::set<std::string> ids, names;
    for (const auto& p : s) {
        ids.insert(p.id); names.insert(p.name);
        CHECK(std::string(p.id).rfind("in07.", 0) == 0);
        CHECK(p.def >= std::min(p.min, p.max));
        CHECK(p.def <= std::max(p.min, p.max));
    }
    CHECK(ids.size() == s.size());
    CHECK(names.size() == s.size());
    CHECK(std::string(s[Voices].id) == "in07.voices");
    CHECK(s[Voices].def == 16);
    CHECK(s[Voices].numSteps() == 32);
    CHECK(s[Mode].numSteps() == 3);
    CHECK(s[Bend].def == 2);
    CHECK(std::string(s[lp(0, Cutoff)].id) == "in07.l1.flt.cutoff");
    CHECK(std::string(s[lp(3, Wave)].id) == "in07.l4.osc.wave");
    CHECK(std::string(s[lp(1, Cutoff)].name) == "L2 Cutoff");
    for (int l = 0; l < kLayers; ++l) {
        CHECK(s[lp(l, On)].def == (l == 0 ? 1 : 0));    // L1 on, the others off
        CHECK(s[lp(l, Cutoff)].curve == sw::Curve::Log);
        CHECK(s[lp(l, Cutoff)].def == doctest::Approx(2400));
        CHECK(s[lp(l, AmpA)].curve == sw::Curve::Log);
        CHECK(s[lp(l, Wave)].numSteps() == 4);
        CHECK(s[lp(l, FilterType)].numSteps() == 4);
        CHECK(s[lp(l, Unison)].numSteps() == 8);
        CHECK(s[lp(l, Semi)].numSteps() == 25);
    }
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

TEST_CASE("IN07: pitch, octave, semitones and fine tune") {
    Processor p; plain(p);
    p.noteOn(69, 1.0);
    auto y = render(p, 48000).first;
    CHECK(tu::binDb(y, 440, 24000, 48000) > tu::binDb(y, 430, 24000, 48000) + 30);
    CHECK(tu::binDb(y, 440, 24000, 48000) > tu::binDb(y, 450, 24000, 48000) + 30);
    p.allNotesOff(); render(p, 4800);
    p.setParam(lp(0, Octave), -1);
    p.noteOn(69, 1.0);
    y = render(p, 48000).first;
    CHECK(tu::binDb(y, 220, 24000, 48000) > tu::binDb(y, 440, 24000, 48000) + 40);
    p.allNotesOff(); render(p, 4800);
    p.setParam(lp(0, Octave), 0); p.setParam(lp(0, Semi), 7); p.setParam(lp(0, Fine), -50);
    p.noteOn(69, 1.0); render(p, 480);
    CHECK(v0(p, 69).frequency() == doctest::Approx(440.0 * std::exp2(6.5 / 12.0)).epsilon(1e-9));
}

TEST_CASE("IN07: LP24 at Res 0 is a 4th-order Butterworth") {
    Processor p; plain(p); p.setParam(lp(0, FilterType), LP24);
    const double open = heldDb(p, 83);                 // 987.8 Hz, cutoff 20 kHz
    p.setParam(lp(0, Cutoff), midiHz(83));
    CHECK(heldDb(p, 83) - open == doctest::Approx(-3.01).epsilon(0.03));
    p.setParam(lp(0, Cutoff), 20000);
    const double open71 = heldDb(p, 71);               // 493.9 Hz
    p.setParam(lp(0, Cutoff), midiHz(71) / 2);         // one octave below the note: 10 log10(1 + 2^8) = 24.1 dB down
    CHECK(heldDb(p, 71) - open71 == doctest::Approx(-24.1).epsilon(0.015));
}

TEST_CASE("IN07: resonance, HP12 and BP12") {
    Processor p; plain(p);
    const double open = heldDb(p, 81);                 // 880 Hz
    p.setParam(lp(0, Cutoff), 880);
    CHECK(heldDb(p, 81) - open == doctest::Approx(-3.01).epsilon(0.03));
    p.setParam(lp(0, Resonance), 100);
    CHECK(heldDb(p, 81) - open > 20.0);
    p.setParam(lp(0, Resonance), 0); p.setParam(lp(0, FilterType), HP12);
    CHECK(heldDb(p, 81) - open == doctest::Approx(-3.01).epsilon(0.03));
    CHECK(heldDb(p, 69) - open < -11.0);               // an octave below a 2nd-order high-pass: about -12.3 dB
    p.setParam(lp(0, FilterType), BP12);
    CHECK(heldDb(p, 81) - open == doctest::Approx(0.0).epsilon(0.02));
    CHECK(heldDb(p, 105) - open < -5.0);              // two octaves above the centre: about -9 dB
}

TEST_CASE("IN07: filter envelope and key tracking move the cutoff") {
    Processor p; plain(p);
    p.setParam(lp(0, Cutoff), 200); p.setParam(lp(0, FilterEnv), 100);
    p.setParam(lp(0, FenvA), 0.5); p.setParam(lp(0, FenvD), 5000); p.setParam(lp(0, FenvS), 100); p.setParam(lp(0, FenvR), 1000);
    p.noteOn(60, 1.0); render(p, 4800);
    CHECK(v0(p, 60).cutoffInUse() == doctest::Approx(200 * 32.0).epsilon(0.01));
    p.allNotesOff(); render(p, 96000);
    p.setParam(lp(0, FilterEnv), -100);
    p.noteOn(60, 1.0); render(p, 4800);
    CHECK(v0(p, 60).cutoffInUse() == doctest::Approx(20.0));
    p.allNotesOff(); render(p, 96000);
    p.setParam(lp(0, FilterEnv), 0); p.setParam(lp(0, KeyTrack), 100);
    p.noteOn(60, 1.0); render(p, 960);
    const double c60 = v0(p, 60).cutoffInUse();
    p.allNotesOff(); render(p, 4800);
    p.noteOn(72, 1.0); render(p, 960);
    CHECK(v0(p, 72).cutoffInUse() / c60 == doctest::Approx(2.0).epsilon(0.001));
}

TEST_CASE("IN07: amp envelope timing, sustain, release and sleep") {
    Processor p; plain(p);
    p.setParam(lp(0, AmpA), 10); p.setParam(lp(0, AmpD), 100); p.setParam(lp(0, AmpS), 50); p.setParam(lp(0, AmpR), 200);
    render(p, 64);                                     // the parameters reach the voice
    p.noteOn(60, 1.0);
    const auto& env = v0(p, 60).ampEnv();
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

TEST_CASE("IN07: the same key again re-triggers its own voice from the current level") {
    Processor p; plain(p);
    p.setParam(lp(0, AmpA), 5); p.setParam(lp(0, AmpR), 500);
    p.noteOn(60, 1.0); render(p, 4800);
    p.noteOff(60); render(p, 4800);
    const auto& env = v0(p, 60).ampEnv();
    double prev = env.level(), maxStep = 0;
    CHECK(prev > 0.1);
    p.noteOn(60, 1.0);
    CHECK(p.notes() == 1);                             // no second voice for the same key
    for (int i = 0; i < 480; ++i) { render(p, 1, 1); maxStep = std::max(maxStep, std::abs(env.level() - prev)); prev = env.level(); }
    CHECK(maxStep < 0.02);
}

TEST_CASE("IN07: a chord is the sum of its notes") {
    auto play = [](std::vector<int> keys) {
        Processor p; plain(p);
        for (int k : keys) p.noteOn(k, 1.0);
        return render(p, 9600).first;
    };
    const auto chord = play({60, 64, 67});
    const auto a = play({60}), b = play({64}), c = play({67});
    double err = 0;
    for (size_t i = 0; i < chord.size(); ++i) err = std::max(err, std::abs(static_cast<double>(chord[i]) - a[i] - b[i] - c[i]));
    CHECK(err < 1e-5);
    CHECK(tu::rmsDb(chord, 4800, 9600) > tu::rmsDb(a, 4800, 9600) + 3.0);
}

TEST_CASE("IN07: the voice limit steals the oldest note, released ones first, with a short fade") {
    Processor p; plain(p); p.setParam(Voices, 2); p.setParam(lp(0, AmpR), 2000);
    p.noteOn(60, 1.0); render(p, 480);
    p.noteOn(64, 1.0); render(p, 480);
    p.noteOn(67, 1.0);                                 // a third note: 60 (the oldest) goes
    CHECK(p.notes() == 2);
    render(p, 240);                                    // 5 ms: the stolen voice has faded out (3 ms)
    CHECK(p.find(60) == nullptr);
    CHECK(p.find(64) != nullptr);
    CHECK(p.find(67) != nullptr);
    int key = 0, ch = 0, id = 0; bool ended60 = false;
    while (p.takeEnded(key, ch, id)) ended60 = ended60 || key == 60;
    CHECK(ended60);
    // a released note goes before an older held one
    p.noteOff(67); render(p, 480);                     // 67 is releasing (2 s), 64 is held and older
    p.noteOn(72, 1.0); render(p, 240);
    CHECK(p.find(67) == nullptr);
    CHECK(p.find(64) != nullptr);
    CHECK(p.find(72) != nullptr);
}

TEST_CASE("IN07: the stolen voice fades instead of clicking") {
    Processor p; plain(p); p.setParam(Voices, 1);
    p.setParam(lp(0, Wave), Sine);
    p.noteOn(48, 1.0); render(p, 4800);
    p.noteOn(84, 1.0);
    auto y = render(p, 480).first;
    // the old 130.8 Hz sine may move at most by its own slope plus the fade per sample (no jump to 0)
    double maxStep = 0;
    for (size_t i = 1; i < y.size(); ++i) maxStep = std::max(maxStep, std::abs(static_cast<double>(y[i]) - y[i - 1]));
    CHECK(maxStep < 0.25);                             // the new 1047 Hz sine alone moves up to 0.137 a sample
}

TEST_CASE("IN07: layers stack, each with its own level, pan and pitch") {
    Processor p; plain(p);
    plainLayer(p, 1); p.setParam(lp(1, Octave), 1);
    p.noteOn(57, 1.0);
    auto y = render(p, 48000).first;
    CHECK(tu::binDb(y, 440, 24000, 48000) - tu::binDb(y, 220, 24000, 48000) == doctest::Approx(0.0).epsilon(0.05));
    p.allNotesOff(); render(p, 4800);
    p.setParam(lp(1, LayerLevel), -12);
    p.noteOn(57, 1.0);
    y = render(p, 48000).first;
    CHECK(tu::binDb(y, 440, 24000, 48000) - tu::binDb(y, 220, 24000, 48000) == doctest::Approx(-12.0).epsilon(0.02));
    p.allNotesOff(); render(p, 4800);
    p.setParam(lp(1, LayerLevel), -60);                // the bottom of the range is Off
    p.noteOn(57, 1.0);
    y = render(p, 48000).first;
    CHECK(tu::binDb(y, 440, 24000, 48000) - tu::binDb(y, 220, 24000, 48000) < -100.0);
    p.allNotesOff(); render(p, 4800);
    // pan: L1 hard left, L2 hard right
    p.setParam(lp(1, LayerLevel), 0); p.setParam(lp(0, Pan), -100); p.setParam(lp(1, Pan), 100);
    p.noteOn(57, 1.0);
    auto st = render(p, 48000);
    CHECK(tu::binDb(st.first, 440, 24000, 48000) < -100.0);
    CHECK(tu::binDb(st.second, 220, 24000, 48000) < -100.0);
    CHECK(tu::binDb(st.first, 220, 24000, 48000) > -20.0);
}

TEST_CASE("IN07: a layer turned off fades its sounding voices") {
    Processor p; plain(p); plainLayer(p, 1); p.setParam(lp(1, Octave), 1); p.setParam(lp(1, AmpR), 2000);
    p.noteOn(57, 1.0); render(p, 4800);
    CHECK(p.find(57, 1) != nullptr);
    p.setParam(lp(1, On), 0);
    render(p, 480);
    CHECK(p.find(57, 1) == nullptr);
    CHECK(p.find(57, 0) != nullptr);
}

TEST_CASE("IN07: pitch bend follows the bend range") {
    Processor p; plain(p);
    p.noteOn(69, 1.0); render(p, 480);
    p.pitchBend(1.0); render(p, 480);
    CHECK(v0(p, 69).frequency() == doctest::Approx(440.0 * std::exp2(2.0 / 12.0)).epsilon(1e-9));
    p.setParam(Bend, 12); render(p, 480);
    CHECK(v0(p, 69).frequency() == doctest::Approx(880.0).epsilon(1e-9));
    p.pitchBend(-0.5); render(p, 480);
    CHECK(v0(p, 69).frequency() == doctest::Approx(440.0 / std::sqrt(2.0)).epsilon(1e-9));
}

TEST_CASE("IN07: the sustain pedal holds released notes until it is lifted") {
    Processor p; plain(p); p.setParam(lp(0, AmpR), 50);
    p.sustain(true);
    p.noteOn(60, 1.0); render(p, 480);
    p.noteOff(60); render(p, 24000);
    CHECK(p.find(60) != nullptr);
    CHECK(v0(p, 60).ampEnv().stage() == Adsr::Sustain);
    p.sustain(false); render(p, 4800);                 // 50 ms release, then nothing
    CHECK(p.find(60) == nullptr);
    CHECK_FALSE(p.active());
}

TEST_CASE("IN07: mono re-triggers, legato glides without a new attack") {
    Processor p; plain(p); p.setParam(Mode, Mono); p.setParam(lp(0, AmpA), 50); p.setParam(lp(0, AmpD), 100); p.setParam(lp(0, AmpS), 50);
    p.noteOn(60, 1.0); render(p, 9600);
    CHECK(v0(p, 60).ampEnv().stage() == Adsr::Sustain);
    p.noteOn(64, 1.0); render(p, 48);
    CHECK(p.notes() == 1);
    CHECK(p.find(60) == nullptr);
    CHECK(v0(p, 64).ampEnv().stage() == Adsr::Attack);  // mono: a new attack
    render(p, 9600);
    p.noteOff(64); render(p, 48);                       // back to the key still held
    CHECK(v0(p, 60).ampEnv().stage() == Adsr::Attack);
    p.allNotesOff(); render(p, 9600);

    p.setParam(Mode, Legato); p.setParam(Glide, 100);
    p.noteOn(60, 1.0); render(p, 9600);
    CHECK(v0(p, 60).frequency() == doctest::Approx(midiHz(60)).epsilon(1e-9));   // the first note does not glide
    p.noteOn(72, 1.0); render(p, 48);
    CHECK(p.notes() == 1);
    CHECK(v0(p, 72).ampEnv().stage() == Adsr::Sustain); // legato: no new attack
    render(p, 2400 - 48);                              // half way through the 100 ms glide (control rate: within 32 samples)
    CHECK(v0(p, 72).frequency() == doctest::Approx(midiHz(66)).epsilon(0.004));
    render(p, 4800);
    CHECK(v0(p, 72).frequency() == doctest::Approx(midiHz(72)).epsilon(1e-9));
    p.noteOff(72); render(p, 4800);
    CHECK(v0(p, 60).frequency() == doctest::Approx(midiHz(60)).epsilon(1e-9));
    CHECK(v0(p, 60).ampEnv().stage() == Adsr::Sustain);
}

TEST_CASE("IN07: poly glide starts from the previous note while one is held") {
    Processor p; plain(p); p.setParam(Glide, 200);
    p.noteOn(57, 1.0); render(p, 4800);
    CHECK(v0(p, 57).frequency() == doctest::Approx(220.0).epsilon(0.001));
    p.noteOn(69, 1.0); render(p, 4800);                // half way: 100 ms of 200 ms
    CHECK(v0(p, 69).frequency() == doctest::Approx(220.0 * std::sqrt(2.0)).epsilon(0.01));
    CHECK(v0(p, 57).frequency() == doctest::Approx(220.0).epsilon(0.001));   // the held note stays
    render(p, 9600);
    CHECK(v0(p, 69).frequency() == doctest::Approx(440.0).epsilon(0.001));
}

TEST_CASE("IN07: every note reports its end once (CLAP note end), also with all layers off") {
    Processor p; plain(p);
    p.noteOn(60, 1.0, 3, 1001); render(p, 480);
    int key = 0, ch = 0, id = 0;
    CHECK_FALSE(p.takeEnded(key, ch, id));
    p.noteOff(60, 3); render(p, 4800);
    REQUIRE(p.takeEnded(key, ch, id));
    CHECK(key == 60); CHECK(ch == 3); CHECK(id == 1001);
    CHECK_FALSE(p.takeEnded(key, ch, id));
    p.setParam(lp(0, On), 0);
    p.noteOn(62, 1.0, 0, 7); render(p, 32);
    CHECK_FALSE(p.active());
    REQUIRE(p.takeEnded(key, ch, id));
    CHECK(key == 62); CHECK(id == 7);
}

TEST_CASE("IN07: the block size does not change the output") {
    auto play = [](int block) {
        Processor p; plain(p);
        p.setParam(lp(0, Wave), Saw); p.setParam(lp(0, Unison), 4); p.setParam(lp(0, Detune), 30); p.setParam(lp(0, Spread), 80);
        p.setParam(lp(0, FilterType), LP24); p.setParam(lp(0, Cutoff), 900); p.setParam(lp(0, Resonance), 40); p.setParam(lp(0, FilterEnv), 50);
        p.setParam(lp(0, Drive), 30);
        plainLayer(p, 2); p.setParam(lp(2, Wave), Square); p.setParam(lp(2, Octave), -1); p.setParam(lp(2, Pan), 40);
        p.noteOn(57, 0.8); p.noteOn(64, 0.6);
        auto a = render(p, 9000, block);
        p.noteOff(57); p.pitchBend(0.3);
        auto b = render(p, 9000, block);
        a.first.insert(a.first.end(), b.first.begin(), b.first.end());
        a.first.insert(a.first.end(), b.second.begin(), b.second.end());
        return a.first;
    };
    const auto x = play(256), y = play(37);
    REQUIRE(x.size() == y.size());
    size_t diff = 0; for (size_t i = 0; i < x.size(); ++i) if (x[i] != y[i]) ++diff;
    CHECK(diff == 0);
}

TEST_CASE("IN07: unison spread and level") {
    auto play = [](int voices, double spread) {
        Processor p; plain(p);
        p.setParam(lp(0, Wave), Saw); p.setParam(lp(0, Unison), voices); p.setParam(lp(0, Detune), 30); p.setParam(lp(0, Spread), spread);
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
    p.setParam(lp(0, Drive), 100);
    p.noteOn(57, 1.0);
    y = render(p, 48000).first;
    CHECK(tu::harmDb(y, 220, 3) > -40.0);
}

TEST_CASE("IN07: silent and asleep with no notes; safe before prepare") {
    Processor q;
    q.setParam(lp(0, Cutoff), 500); q.noteOn(60, 1.0); q.noteOff(60); q.pitchBend(0.5); q.sustain(true); q.snapToTargets();
    std::vector<float> l(64, 1.0f), r(64, 1.0f); float* c[2] = {l.data(), r.data()};
    q.process(c, 2, 64);
    CHECK(*std::max_element(l.begin(), l.end()) == 0.0f);
    Processor p; plain(p);
    auto y = render(p, 4800);
    CHECK(*std::max_element(y.first.begin(), y.first.end()) == 0.0f);
    CHECK_FALSE(p.active());
}

TEST_CASE("IN07: finite and bounded under random settings and chords") {
    tu::Gauss g(7);
    Processor p; p.prepare(kFs, 256);
    const auto& s = specs();
    double peak = 0; bool finite = true;
    for (int round = 0; round < 30; ++round) {
        for (int id = 0; id < kNumParams; ++id) p.setParam(id, s[static_cast<size_t>(id)].toValue(g.uni()));
        for (int l = 0; l < kLayers; ++l) { p.setParam(lp(l, AmpR), 20); p.setParam(lp(l, LayerLevel), -6); }
        p.setParam(Level, 0);
        const int n = 1 + static_cast<int>(g.uni() * 5);
        for (int k = 0; k < n; ++k) p.noteOn(24 + static_cast<int>(g.uni() * 84), g.uni());
        p.pitchBend(2.0 * g.uni() - 1.0);
        auto y = render(p, 2400, 1 + static_cast<int>(g.uni() * 300));
        p.allNotesOff();
        auto z = render(p, 2400);
        for (float v : y.first) { finite = finite && std::isfinite(v); peak = std::max(peak, static_cast<double>(std::abs(v))); }
        for (float v : z.second) { finite = finite && std::isfinite(v); peak = std::max(peak, static_cast<double>(std::abs(v))); }
    }
    CHECK(finite);
    CHECK(peak < 100.0);
}

// ---- the effects after the voices (6 slots, any order)
namespace {
void fxOff(Processor& p) { for (int f = 0; f < kFx; ++f) p.setParam(fxOnId(f), 0); }
// a held note through one effect, steady part (dB RMS of the left channel)
double fxHeldDb(Processor& p, int note, double seconds = 1.0) {
    p.noteOn(note, 1.0);
    auto y = render(p, static_cast<size_t>(seconds * kFs)).first;
    p.allNotesOff(); render(p, 9600);
    return tu::rmsDb(y, y.size() / 2, y.size());
}
}  // namespace

TEST_CASE("IN07 FX: parameter table and order slots") {
    const auto& s = specs();
    CHECK(PresetSelect == kNumGlobal + kLayers * kLayerParams + kFxParams + (kModSlotBase - kFxEnd) + kModSlots * kModFields);   // then the preset selector
    CHECK(kNumParams == PresetSelect + 1 + (7 + 2 * kArpSteps) + (3 + kArpSteps));   // then the arp (7 + 16 x 2) and the gate (3 + 16), appended 2026-10-09
    CHECK(std::string(s[FxSlot1].id) == "in07.fx.slot1");
    CHECK(std::string(s[FxDelayTime].id) == "in07.fx.delay.time");
    CHECK(std::string(s[FxReverbSize].name) == "Reverb size");
    for (int i = 0; i < kFx; ++i) {
        CHECK_FALSE(s[FxSlot1 + i].automatable);       // the order is not for automation
        CHECK(s[FxSlot1 + i].def == i);                 // Drive, Chorus, Delay, Reverb, EQ, Limit
        CHECK(s[FxSlot1 + i].numSteps() == kFx);
    }
    CHECK(s[fxOnId(FxEq)].def == 0);                    // the screen's defaults: EQ off, the rest on
    CHECK(s[fxOnId(FxDrive)].def == 1);
    CHECK(s[FxDelayTime].numSteps() == 6);
}

TEST_CASE("IN07 FX: the order follows the slots; a repeated effect keeps one place, missing ones go to the end") {
    Processor p; plain(p);
    p.setParam(FxSlot1, FxEq); p.setParam(FxSlot2, FxDrive);
    auto o = p.fxOrder();
    CHECK(o[0] == FxEq); CHECK(o[1] == FxDrive);
    std::set<int> all(o.begin(), o.end());
    CHECK(all.size() == static_cast<size_t>(kFx));
    p.setParam(FxSlot3, FxEq);                          // EQ twice: the first place counts, Delay (pushed out) goes to the end
    o = p.fxOrder();
    all = std::set<int>(o.begin(), o.end());
    CHECK(all.size() == static_cast<size_t>(kFx));
    CHECK(o[0] == FxEq);
}

TEST_CASE("IN07 FX: with every effect off the output is the voices alone") {
    Processor a; plain(a); fxOff(a);
    Processor b; plain(b); fxOff(b); b.setParam(FxReverbMix, 100); b.setParam(FxDriveAmount, 100);   // settings of switched-off effects do nothing
    a.noteOn(60, 1.0); b.noteOn(60, 1.0);
    auto x = render(a, 9600).first, y = render(b, 9600).first;
    size_t diff = 0; for (size_t i = 0; i < x.size(); ++i) if (x[i] != y[i]) ++diff;
    CHECK(diff == 0);
}

TEST_CASE("IN07 FX: EQ bands") {
    Processor p; plain(p); fxOff(p);
    const double d50 = fxHeldDb(p, 31), d1k = fxHeldDb(p, 84), d12k = fxHeldDb(p, 127);   // 49 Hz, 1047 Hz, 12.5 kHz
    p.setParam(fxOnId(FxEq), 1);
    CHECK(fxHeldDb(p, 84) - d1k == doctest::Approx(0.0).epsilon(0.02));     // flat at 0 dB
    p.setParam(FxEqLow, 12);
    CHECK(fxHeldDb(p, 31) - d50 == doctest::Approx(12.0).epsilon(0.03));
    p.setParam(FxEqLow, 0); p.setParam(FxEqMid, -12);
    CHECK(fxHeldDb(p, 84) - d1k == doctest::Approx(-12.0).epsilon(0.05));
    p.setParam(FxEqMid, 0); p.setParam(FxEqHigh, 12);
    CHECK(fxHeldDb(p, 127) - d12k == doctest::Approx(12.0).epsilon(0.05));
}

TEST_CASE("IN07 FX: delay time follows the tempo, echoes alternate sides (ping-pong)") {
    Processor p; plain(p); fxOff(p);
    p.setParam(lp(0, AmpR), 1); p.setParam(fxOnId(FxDelay), 1); p.setParam(FxDelayMix, 100); p.setParam(FxDelayFeedback, 50);
    p.setParam(FxDelayTime, 3);                          // 1/4
    p.setTempo(120.0);                                   // a quarter = 0.5 s
    p.noteOn(81, 1.0); render(p, 480); p.noteOff(81);    // a 10 ms blip
    auto y = render(p, 96000);
    auto energy = [](const std::vector<float>& x, double a, double b) { double s = 0; for (size_t i = static_cast<size_t>(a * kFs); i < static_cast<size_t>(b * kFs); ++i) s += x[i] * x[i]; return s; };
    // first echo at 0.5 s after the blip (which started 480 samples before this render)
    const double e1L = energy(y.first, 0.49, 0.52), e1R = energy(y.second, 0.49, 0.52);
    const double e2L = energy(y.first, 0.99, 1.02), e2R = energy(y.second, 0.99, 1.02);
    const double gap = energy(y.first, 0.2, 0.45) + energy(y.second, 0.2, 0.45);
    CHECK(e1L + e1R > 1000 * gap);
    CHECK(e1L > 100 * e1R);                              // the first echo on the left
    CHECK(e2R > 100 * e2L);                              // the second on the right
    p.allSoundOff(); render(p, 96000);
    p.setTempo(60.0);                                     // a quarter = 1 s
    p.noteOn(81, 1.0); render(p, 480); p.noteOff(81);
    y = render(p, 96000);
    CHECK(energy(y.first, 0.99, 1.02) > 1000 * energy(y.first, 0.45, 0.55));
}

TEST_CASE("IN07 FX: reverb decays in about its size") {
    Processor p; plain(p); fxOff(p);
    p.setParam(lp(0, Wave), Saw); p.setParam(lp(0, AmpR), 1);
    p.setParam(fxOnId(FxReverb), 1); p.setParam(FxReverbMix, 100); p.setParam(FxReverbSize, 2.0); p.setParam(FxReverbDamp, 0);
    p.noteOn(60, 1.0); render(p, 24000); p.noteOff(60);
    auto y = render(p, static_cast<size_t>(3.0 * kFs)).first;
    // the decay from 0.3 s to 1.3 s should be about 30 dB (60 dB in 2 s)
    const double a = tu::rmsDb(y, static_cast<size_t>(0.25 * kFs), static_cast<size_t>(0.35 * kFs));
    const double b = tu::rmsDb(y, static_cast<size_t>(1.25 * kFs), static_cast<size_t>(1.35 * kFs));
    MESSAGE("reverb: " << a - b << " dB in 1 s (size 2 s)");
    CHECK(a - b == doctest::Approx(30.0).epsilon(0.25));
}

TEST_CASE("IN07 FX: chorus widens a mono voice at about the same level") {
    Processor p; plain(p); fxOff(p); p.setParam(lp(0, Wave), Saw);
    p.noteOn(57, 1.0);
    auto dry = render(p, 48000);
    p.allNotesOff(); render(p, 9600);
    p.setParam(fxOnId(FxChorus), 1); p.setParam(FxChorusMix, 50); p.setParam(FxChorusDepth, 60);
    p.noteOn(57, 1.0);
    auto wet = render(p, 48000);
    double sl = 0, sr = 0, slr = 0;
    for (size_t i = 24000; i < 48000; ++i) { sl += wet.first[i] * wet.first[i]; sr += wet.second[i] * wet.second[i]; slr += wet.first[i] * wet.second[i]; }
    CHECK(slr / std::sqrt(sl * sr) < 0.95);
    CHECK(std::abs(tu::rmsDb(wet.first, 24000, 48000) - tu::rmsDb(dry.first, 24000, 48000)) < 3.0);
}

TEST_CASE("IN07 FX: drive adds harmonics, limit holds the ceiling, the order matters") {
    Processor p; plain(p); fxOff(p);
    p.setParam(fxOnId(FxDrive), 1); p.setParam(FxDriveAmount, 100); p.setParam(FxDriveTone, 100); p.setParam(FxDriveMix, 100);
    p.noteOn(57, 1.0);
    auto y = render(p, 48000).first;
    p.allNotesOff(); render(p, 9600);
    CHECK(tu::harmDb(y, 220, 3) > -40.0);
    fxOff(p);
    // a loud chord through Limit: +12 dB of gain, ceiling -3 dB
    p.setParam(lp(0, Wave), Saw); p.setParam(lp(0, Unison), 4); p.setParam(fxOnId(FxLimit), 1); p.setParam(FxLimitGain, 12); p.setParam(FxLimitCeiling, -3);
    render(p, 480);                                      // the 5 ms switch fades are over
    for (int k : {48, 52, 55, 60, 64}) p.noteOn(k, 1.0);
    auto st = render(p, 48000);
    p.allNotesOff(); render(p, 48000);
    double peak = 0; for (size_t i = 0; i < st.first.size(); ++i) peak = std::max({peak, std::abs(static_cast<double>(st.first[i])), std::abs(static_cast<double>(st.second[i]))});
    CHECK(20 * std::log10(peak) <= -3.0 + 0.01);
    CHECK(20 * std::log10(peak) > -6.0);
    // EQ then Drive differs from Drive then EQ
    auto play = [](int first, int second) {
        Processor q; plain(q); fxOff(q);
        q.setParam(fxOnId(FxDrive), 1); q.setParam(FxDriveAmount, 80); q.setParam(fxOnId(FxEq), 1); q.setParam(FxEqLow, 12);
        q.setParam(FxSlot1, first); q.setParam(FxSlot2, second);
        q.noteOn(36, 1.0);
        return render(q, 9600).first;
    };
    auto a = play(FxEq, FxDrive), b = play(FxDrive, FxEq);
    double d = 0; for (size_t i = 0; i < a.size(); ++i) d = std::max(d, std::abs(static_cast<double>(a[i]) - b[i]));
    CHECK(d > 0.01);
}

TEST_CASE("IN07 FX: switching an effect fades instead of clicking") {
    Processor p; plain(p); fxOff(p);
    p.setParam(lp(0, Wave), Sine);
    p.noteOn(57, 1.0); render(p, 4800);
    p.setParam(fxOnId(FxEq), 1); p.setParam(FxEqLow, 12);   // +12 dB on a 220 Hz sine: a jump would be a click
    auto y = render(p, 4800).first;
    double maxStep = 0;
    for (size_t i = 1; i < y.size(); ++i) maxStep = std::max(maxStep, std::abs(static_cast<double>(y[i]) - y[i - 1]));
    CHECK(maxStep < 0.12);   // a 220 Hz sine of amplitude 1..3 moves at most 0.029..0.086 a sample
}

TEST_CASE("IN07 FX: the effects go to sleep after their tails, and wake with the next note") {
    Processor p; p.prepare(kFs, 256);                    // the default patch: drive, chorus, delay, reverb and limit on
    p.noteOn(60, 1.0); render(p, 24000); p.noteOff(60);
    auto tail = render(p, 48000);
    CHECK(tu::rmsDb(tail.first, 0, 24000) > -60.0);     // the reverb and the delay ring on
    CHECK_FALSE(p.fxAsleep());
    auto later = render(p, static_cast<size_t>(12.0 * kFs));
    CHECK(p.fxAsleep());
    CHECK(*std::max_element(later.first.end() - 4800, later.first.end()) == 0.0f);
    CHECK(*std::min_element(later.second.end() - 4800, later.second.end()) == 0.0f);
    p.noteOn(64, 1.0);
    auto again = render(p, 4800);
    CHECK_FALSE(p.fxAsleep());
    CHECK(tu::rmsDb(again.first, 2400, 4800) > -40.0);
}

// ---- modulation: LFOs (with the orbit shape), the matrix, the macros; the planet features flyby and gravity
namespace {
// one matrix slot
void route(Processor& p, int slot, int src, int dst, double amount) {
    p.setParam(modId(slot, ModSrc), src); p.setParam(modId(slot, ModDst), dst); p.setParam(modId(slot, ModAmount), amount); p.setParam(modId(slot, ModOn), 1);
}
// the frequency of a voice, sampled every `step` samples for `n` samples
std::vector<double> freqTrack(Processor& p, int key, size_t n, int step) {
    std::vector<double> f;
    for (size_t i = 0; i < n; i += static_cast<size_t>(step)) { render(p, static_cast<size_t>(step), step); f.push_back(v0(p, key).frequency()); }
    return f;
}
}  // namespace

TEST_CASE("IN07 MOD: parameter table") {
    const auto& s = specs();
    CHECK(std::string(s[Lfo1Shape].id) == "in07.lfo1.shape");
    CHECK(std::string(s[Lfo2Rate].id) == "in07.lfo2.rate");
    CHECK(std::string(s[Macro1].name) == "M1 Bright");
    CHECK(std::string(s[Macro8].name) == "M8 Reverb");
    CHECK(std::string(s[modId(0, ModSrc)].id) == "in07.mod1.src");
    CHECK(std::string(s[modId(7, ModAmount)].id) == "in07.mod8.amount");
    CHECK(std::string(s[FlybyMode].id) == "in07.flyby.mode");
    CHECK(std::string(s[lp(2, Gravity)].id) == "in07.l3.osc.gravity");
    for (int m = 0; m < 8; ++m) CHECK(s[Macro1 + m].def == 50);   // the middle = the preset as it is
    CHECK(s[modId(0, ModSrc)].numSteps() == kModSources);
    CHECK(s[modId(0, ModDst)].numSteps() == kModDests);
    CHECK(s[FlybyMode].def == FlybyOff);
    CHECK(s[lp(0, Gravity)].def == 0);
}

TEST_CASE("IN07 MOD: the orbit LFO is a cosine at eccentricity 0 and a slingshot near 1") {
    for (int i = 0; i < 64; ++i) {
        const double ph = i / 64.0;
        CHECK(lfoShape(LfoOrbit, ph, 0.0) == doctest::Approx(std::cos(2 * tu::kPi * ph)).epsilon(1e-9));
    }
    // e = 0.8: Kepler's second law, the moon lingers far out (y near -1) and whips round the planet: y is a narrow peak
    // (a cosine is above 0.5 for a third of the cycle; here for 2 (E - e sin E) / 2 pi at E = pi / 3 = 11.3 %)
    int below = 0, top = 0; double maxStep = 0, prev = lfoShape(LfoOrbit, 0.0, 0.8);
    const int n = 4096;
    for (int i = 1; i <= n; ++i) {
        const double y = lfoShape(LfoOrbit, static_cast<double>(i) / n, 0.8);
        below += y < 0.0; top += y > 0.5; maxStep = std::max(maxStep, std::abs(y - prev)); prev = y;
    }
    MESSAGE("orbit e 0.8: " << 100.0 * below / n << " % of the cycle below 0, " << 100.0 * top / n << " % above 0.5, steepest step " << maxStep);
    CHECK(below > 0.70 * n);
    CHECK(static_cast<double>(top) / n == doctest::Approx(0.113).epsilon(0.03));
    CHECK(maxStep > 1.5 * (2 * tu::kPi / n));               // the flanks are steeper than a cosine's (1.67 x at e 0.8)
    for (int sh : {int(LfoTriangle), int(LfoSaw), int(LfoSquare), int(LfoRandom)})
        for (int i = 0; i < 64; ++i) { const double y = lfoShape(sh, i / 64.0, 0.0, 12345u); CHECK(y >= -1.0); CHECK(y <= 1.0); }
}

TEST_CASE("IN07 MOD: LFO to pitch (vibrato), synced to the tempo") {
    Processor p; plain(p);
    p.setParam(Lfo1Shape, LfoOrbit); p.setParam(Lfo1Ecc, 0); p.setParam(Lfo1Rate, 5.0);
    route(p, 0, SrcLfo1, DstPitch, 10.0);                // 10 % of 12 semitones = 1.2
    p.noteOn(69, 1.0);
    auto f = freqTrack(p, 69, 48000, 32);
    const double hi = *std::max_element(f.begin(), f.end()), lo = *std::min_element(f.begin(), f.end());
    CHECK(hi / 440.0 == doctest::Approx(std::exp2(1.2 / 12.0)).epsilon(0.002));
    CHECK(lo / 440.0 == doctest::Approx(std::exp2(-1.2 / 12.0)).epsilon(0.002));
    // cycles in 1 s: count upward crossings of 440 Hz
    auto cycles = [](const std::vector<double>& x) { int c = 0; for (size_t i = 1; i < x.size(); ++i) c += (x[i - 1] < 440.0 && x[i] >= 440.0); return c; };
    CHECK(cycles(f) == 5);
    p.setParam(Lfo1Sync, 5); p.setTempo(120.0);           // 1/4 at 120 bpm = 2 Hz
    f = freqTrack(p, 69, 48000, 32);
    CHECK(cycles(f) == 2);
}

TEST_CASE("IN07 MOD: velocity, mod wheel, aftertouch and key as sources") {
    Processor p; plain(p);
    p.setParam(lp(0, Cutoff), 500);
    route(p, 0, SrcVelocity, DstCutoff, 50.0);            // velocity 1 = +2.5 octaves
    p.noteOn(60, 1.0); render(p, 480);
    const double hard = v0(p, 60).cutoffInUse();
    p.allSoundOff(); p.noteOn(60, 0.0); render(p, 480);
    CHECK(hard / v0(p, 60).cutoffInUse() == doctest::Approx(std::exp2(2.5)).epsilon(0.001));
    p.allSoundOff();
    route(p, 0, SrcModWheel, DstL1Level, -100.0);          // the wheel up silences L1
    p.noteOn(60, 1.0);
    const double open = tu::rmsDb(render(p, 9600).first, 4800, 9600);
    p.modWheel(1.0);
    const double shut = tu::rmsDb(render(p, 9600).first, 4800, 9600);
    CHECK(open > -20.0);
    CHECK(shut < -120.0);
    p.allSoundOff(); p.modWheel(0.0);
    route(p, 0, SrcAftertouch, DstPitch, 100.0);          // pressure 0.5 = +6 semitones
    p.noteOn(69, 1.0); p.aftertouch(0.5); render(p, 480);
    CHECK(v0(p, 69).frequency() == doctest::Approx(440.0 * std::sqrt(2.0)).epsilon(1e-6));
    p.allSoundOff(); p.aftertouch(0.0);
    route(p, 0, SrcKey, DstPan, 100.0);                   // key 120 = (120 - 60) / 60 = full right
    p.noteOn(120, 1.0);
    auto st = render(p, 4800);
    CHECK(tu::rmsDb(st.first, 2400, 4800) < -100.0);
}

TEST_CASE("IN07 MOD: the macros' built-in jobs; the middle changes nothing") {
    Processor p; plain(p); p.setParam(lp(0, Cutoff), 1000);
    p.noteOn(60, 1.0); render(p, 480);
    const double c0 = v0(p, 60).cutoffInUse();
    p.setParam(Macro1, 100); render(p, 480);              // BRIGHT +2 octaves
    CHECK(v0(p, 60).cutoffInUse() / c0 == doctest::Approx(4.0).epsilon(1e-6));
    p.setParam(Macro1, 0); render(p, 480);                // -2 octaves
    CHECK(v0(p, 60).cutoffInUse() / c0 == doctest::Approx(0.25).epsilon(1e-6));
    p.setParam(Macro1, 50); render(p, 480);
    CHECK(v0(p, 60).cutoffInUse() == doctest::Approx(c0).epsilon(1e-12));
    // M8 REVERB: +50 points of reverb mix from the middle
    Processor q; plain(q); q.setParam(fxOnId(FxReverb), 1); q.setParam(FxReverbMix, 0); q.setParam(lp(0, AmpR), 1);
    q.noteOn(72, 1.0); render(q, 480); q.noteOff(72);
    const double dry = tu::rmsDb(render(q, 24000).first, 4800, 24000);
    q.setParam(Macro8, 100); render(q, 96000);
    q.noteOn(72, 1.0); render(q, 480); q.noteOff(72);
    const double wet = tu::rmsDb(render(q, 24000).first, 4800, 24000);
    CHECK(dry < -120.0);
    CHECK(wet > -60.0);
    // M3 ATTACK: x4 at the top (5 ms -> 20 ms)
    Processor a; plain(a); a.setParam(lp(0, AmpA), 5); a.setParam(Macro3, 100); render(a, 64);
    a.noteOn(60, 1.0);
    int reached = -1;
    for (int i = 0; i < 4000 && reached < 0; ++i) { render(a, 1, 1); if (v0(a, 60).ampEnv().stage() != Adsr::Attack) reached = i + 1; }
    CHECK(reached == doctest::Approx(960).epsilon(0.02));
}

TEST_CASE("IN07 FLYBY: arrive — from high, far and to the side, landing on the note at the set time") {
    Processor p; plain(p);
    p.setParam(FlybyMode, FlybyArrive); p.setParam(FlybyDepth, 100); p.setParam(FlybyTime, 1.0); p.setParam(FlybyNear, 50); p.setParam(FlybySide, 0);
    p.noteOn(69, 1.0);
    auto f = freqTrack(p, 69, 4800, 32);
    CHECK(f.front() > 440.0 * std::exp2(5.0 / 12.0));      // more than 5 semitones up at the start
    render(p, 48000);                                        // to 1.1 s
    CHECK(v0(p, 69).frequency() == doctest::Approx(440.0).epsilon(1e-9));
    Processor q; plain(q);
    q.setParam(FlybyMode, FlybyArrive); q.setParam(FlybyDepth, 100); q.setParam(FlybyTime, 1.0); q.setParam(FlybyNear, 50); q.setParam(FlybySide, 0);
    q.noteOn(69, 1.0);
    auto a = render(q, 72000);
    const double startL = tu::rmsDb(a.first, 0, 4800), startR = tu::rmsDb(a.second, 0, 4800);
    const double endL = tu::rmsDb(a.first, 57600, 72000), endR = tu::rmsDb(a.second, 57600, 72000);
    MESSAGE("flyby arrive: start L " << startL << " R " << startR << " dB, end L " << endL << " R " << endR << " dB");
    CHECK(endL - startL > 4.0);                               // it comes closer: louder (-9.6 dB of distance, +3 dB of pan at the side)
    CHECK(startL - startR > 6.0);                             // side 0 = from the left
    CHECK(std::abs(endL - endR) < 0.01);                      // and ends in the middle
}

TEST_CASE("IN07 FLYBY: pass — high, the note at half time, then low; leave — falls away after the key is let go") {
    Processor p; plain(p);
    p.setParam(FlybyMode, FlybyPass); p.setParam(FlybyDepth, 100); p.setParam(FlybyTime, 1.0); p.setParam(FlybyNear, 50);
    p.noteOn(69, 1.0);
    auto f = freqTrack(p, 69, 57600, 32);                     // 1.2 s
    const double mid = f[static_cast<size_t>(24000 / 32) - 1];
    CHECK(f.front() > 440.0 * std::exp2(4.0 / 12.0));
    CHECK(mid == doctest::Approx(440.0).epsilon(0.01));
    CHECK(f.back() < 440.0 * std::exp2(-4.0 / 12.0));
    Processor q; plain(q); q.setParam(lp(0, AmpR), 2000);
    q.setParam(FlybyMode, FlybyLeave); q.setParam(FlybyDepth, 100); q.setParam(FlybyTime, 0.5); q.setParam(FlybyNear, 50);
    q.noteOn(69, 1.0); render(q, 9600);
    CHECK(v0(q, 69).frequency() == doctest::Approx(440.0).epsilon(1e-9));   // no flyby while held
    q.noteOff(69); render(q, 24000);
    CHECK(v0(q, 69).frequency() < 440.0 * std::exp2(-4.0 / 12.0));
}

TEST_CASE("IN07 GRAVITY: the unison copies pull into step at the same loudness") {
    auto play = [](double gravity) {
        Processor p; plain(p);
        p.setParam(lp(0, Wave), Saw); p.setParam(lp(0, Unison), 8); p.setParam(lp(0, Detune), 30); p.setParam(lp(0, Spread), 0);
        p.setParam(lp(0, Gravity), gravity);
        p.noteOn(57, 1.0);
        auto y = render(p, 96000).first;
        return std::make_pair(v0(p, 57).coherence(), tu::rmsDb(y, 48000, 96000));
    };
    const auto free = play(0), mid = play(50), full = play(100);
    MESSAGE("gravity 0 / 50 / 100: coherence " << free.first << " / " << mid.first << " / " << full.first << ", level " << free.second << " / " << mid.second << " / " << full.second << " dB");
    CHECK(full.first > 0.97);                                 // locked: one saw
    CHECK(free.first < 0.8);
    CHECK(mid.first > free.first);
    CHECK(std::abs(full.second - free.second) < 2.0);         // the level is held
    CHECK(std::abs(mid.second - free.second) < 2.0);
}

TEST_CASE("IN07 MOD: the block size does not change the output with LFOs, flyby and gravity") {
    auto play = [](int block) {
        Processor p; plain(p);
        p.setParam(lp(0, Wave), Saw); p.setParam(lp(0, Unison), 4); p.setParam(lp(0, Gravity), 40); p.setParam(lp(0, Cutoff), 1200);
        p.setParam(Lfo1Rate, 3.3); p.setParam(Lfo1Ecc, 70); p.setParam(Lfo2Shape, LfoRandom); p.setParam(Lfo2Rate, 7.0);
        route(p, 0, SrcLfo1, DstCutoff, 30); route(p, 1, SrcLfo2, DstPan, 40); route(p, 2, SrcVelocity, DstResonance, 20);
        p.setParam(FlybyMode, FlybyPass); p.setParam(FlybyTime, 0.2);
        p.noteOn(57, 0.8); p.noteOn(64, 0.5);
        auto a = render(p, 9000, block);
        p.noteOff(57); p.modWheel(0.4);
        auto b = render(p, 9000, block);
        a.first.insert(a.first.end(), b.first.begin(), b.first.end());
        a.first.insert(a.first.end(), b.second.begin(), b.second.end());
        return a.first;
    };
    const auto x = play(256), y = play(37);
    REQUIRE(x.size() == y.size());
    size_t diff = 0; for (size_t i = 0; i < x.size(); ++i) if (x[i] != y[i]) ++diff;
    CHECK(diff == 0);
}

// ---- oscillator types: wavetable (8 procedural tables, mipmapped), FM (2 operators), sample (a small generated bank)
namespace {
// L1 as a given type, everything else plain
void typed(Processor& p, int type) { plain(p); p.setParam(lp(0, OscType), type); }
// power of the bins that are not harmonics of k / n over the fundamental's (dB) for a held note on L1
double aliasOfLayer(Processor& p, int note) {
    p.noteOn(note, 1.0);
    render(p, 24000);
    const int n = 65536;
    auto y = render(p, static_cast<size_t>(n)).first;
    p.allSoundOff();
    std::vector<std::complex<double>> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {   // 4-term Blackman-Harris: side lobes under -92 dB, so the floor of this measure is far below what it checks
        const double t = 2 * tu::kPi * i / n;
        x[static_cast<size_t>(i)] = y[static_cast<size_t>(i)] * (0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t));
    }
    sw::Fft f(n); f.forward(x);
    const double f0 = midiHz(note), bin = kFs / n;
    double harm = 0, other = 0;
    for (int b = 2; b < n / 2; ++b) {
        const double fb = b * bin, h = fb / f0, d = std::abs(h - std::round(h)) * f0;
        const double pw = std::norm(x[static_cast<size_t>(b)]);
        if (d < 6 * bin && std::round(h) >= 1) harm += pw; else other += pw;   // the main lobe is +-4 bins
    }
    return 10 * std::log10(other / harm);
}
}  // namespace

TEST_CASE("IN07 OSC: parameter table for the oscillator types") {
    const auto& s = specs();
    CHECK(std::string(s[lp(0, OscType)].id) == "in07.l1.osc.type");
    CHECK(s[lp(0, OscType)].numSteps() == 4);            // Analog, Wavetable, FM, Sample
    CHECK(s[lp(0, OscType)].def == OscAnalog);
    CHECK(s[lp(0, Table)].numSteps() == kWaveTables);
    CHECK(s[lp(0, SampleId)].numSteps() == kSamples);
    CHECK(s[lp(0, FmRatio)].numSteps() == 12);
    CHECK(std::string(s[lp(3, FmIndex)].id) == "in07.l4.fm.index");
    CHECK(std::string(s[lp(2, Position)].id) == "in07.l3.wt.pos");
    CHECK(s[modId(0, ModDst)].numSteps() == kModDests);
}

TEST_CASE("IN07 OSC: the Classic table goes from saw to square, the level stays") {
    Processor p; typed(p, OscWavetable); p.setParam(lp(0, Table), 0); p.setParam(lp(0, Position), 0);
    p.noteOn(57, 1.0); auto saw = render(p, 48000).first; p.allSoundOff();
    CHECK(tu::harmDb(saw, 220, 2) == doctest::Approx(-6.02).epsilon(0.03));
    CHECK(tu::harmDb(saw, 220, 3) == doctest::Approx(-9.54).epsilon(0.03));
    p.setParam(lp(0, Position), 100);
    p.noteOn(57, 1.0); auto sq = render(p, 48000).first; p.allSoundOff();
    CHECK(tu::harmDb(sq, 220, 2) < -60.0);
    CHECK(tu::harmDb(sq, 220, 3) == doctest::Approx(-9.54).epsilon(0.03));
    for (int t = 0; t < kWaveTables; ++t)                  // every table and position plays at about the same loudness
        for (double pos : {0.0, 33.0, 67.0, 100.0}) {
            p.setParam(lp(0, Table), t); p.setParam(lp(0, Position), pos);
            p.noteOn(57, 1.0); auto y = render(p, 24000).first; p.allSoundOff();
            const double db = tu::rmsDb(y, 12000, 24000);
            CHECK(db > -9.0); CHECK(db < -3.0);           // the frames are normalised to an RMS of 0.5 (-6 dB)
        }
}

TEST_CASE("IN07 OSC: wavetables stay clean across the keyboard (mipmaps, table length, cubic read)") {
    Processor p; typed(p, OscWavetable);
    double worstClassic = 0.0, worstSync = 0.0;
    for (int note = 24; note <= 108; note += 6) {
        p.setParam(lp(0, Table), 0); p.setParam(lp(0, Position), 0);
        const double c = aliasOfLayer(p, note);
        p.setParam(lp(0, Table), 2); p.setParam(lp(0, Position), 100);   // Sync, the brightest frame
        const double y = aliasOfLayer(p, note);
        MESSAGE("note " << note << ": classic " << c << " dB, sync " << y << " dB");
        worstClassic = std::max(worstClassic == 0.0 ? -999.0 : worstClassic, c); worstSync = std::max(worstSync == 0.0 ? -999.0 : worstSync, y);
    }
    CHECK(worstClassic < -70.0);
    CHECK(worstSync < -60.0);
}

TEST_CASE("IN07 OSC: FM stays clean high up (index and feedback limits)") {
    Processor p; typed(p, OscWavetable); p.setParam(lp(0, Table), 0); p.setParam(lp(0, Position), 0);
    const double wt = aliasOfLayer(p, 94);                 // 1865 Hz
    p.setParam(lp(0, Table), 2); p.setParam(lp(0, Position), 100);   // Sync, the brightest frame
    const double sync = aliasOfLayer(p, 94);
    p.setParam(lp(0, OscType), OscFm); p.setParam(lp(0, FmRatio), 4); p.setParam(lp(0, FmIndex), 100); p.setParam(lp(0, FmDecay), 10000);
    const double fm = aliasOfLayer(p, 96);                 // 2093 Hz, ratio 3, index 10 rad: the limit holds the bandwidth under 0.45 fs
    // FM with full feedback (ratio 1, index 50 %): the feedback fades as the modulator's harmonics run out of room under Nyquist
    p.setParam(lp(0, FmRatio), 1); p.setParam(lp(0, FmIndex), 50); p.setParam(lp(0, FmFeedback), 100);
    const double fb84 = aliasOfLayer(p, 84), fb96 = aliasOfLayer(p, 96);
    MESSAGE("alias: wavetable classic " << wt << " dB, sync " << sync << " dB, FM " << fm << " dB, FM feedback 100 % at C6 " << fb84 << " dB, C7 " << fb96 << " dB");
    CHECK(wt < -80.0);
    CHECK(sync < -80.0);
    CHECK(fm < -50.0);                                     // the index limit: the first sidebands past Nyquist under -50 dB together
    CHECK(fb84 < -50.0);
    CHECK(fb96 < -50.0);
}

TEST_CASE("IN07 OSC: FM sidebands follow Bessel, the index decays") {
    Processor p; typed(p, OscFm);
    p.setParam(lp(0, FmRatio), 5); p.setParam(lp(0, FmIndex), 0); p.setParam(lp(0, FmDecay), 10000); p.setParam(lp(0, FmFeedback), 0);
    CHECK(fmRatioOf(5) == 4.0);
    p.noteOn(57, 1.0); auto y = render(p, 48000).first; p.allSoundOff();
    CHECK(tu::harmDb(y, 220, 5) < -90.0);                   // index 0: a sine
    p.setParam(lp(0, FmIndex), 10);                          // 1 rad
    p.noteOn(57, 1.0); y = render(p, 48000).first; p.allSoundOff();
    // carrier f, modulator 4 f: f (J0), 5 f and 3 f (J1), 9 f and 7 f (J2)
    CHECK(tu::harmDb(y, 220, 5) == doctest::Approx(20 * std::log10(0.4400506 / 0.7651977)).epsilon(0.01));
    CHECK(tu::harmDb(y, 220, 3) == doctest::Approx(20 * std::log10(0.4400506 / 0.7651977)).epsilon(0.01));
    CHECK(tu::harmDb(y, 220, 9) == doctest::Approx(20 * std::log10(0.1149035 / 0.7651977)).epsilon(0.02));
    // decay: the sidebands fall away
    p.setParam(lp(0, FmIndex), 40); p.setParam(lp(0, FmDecay), 100);
    p.noteOn(57, 1.0);
    auto early = render(p, 4800).first;
    render(p, 19200);
    auto late = render(p, 24000).first;
    CHECK(tu::binDb(early, 1100, 0, 4800) - tu::binDb(early, 220, 0, 4800) > tu::binDb(late, 1100, 0, 24000) - tu::binDb(late, 220, 0, 24000) + 20.0);
}

TEST_CASE("IN07 OSC: samples loop or end, and follow the key") {
    Processor p; typed(p, OscSample);
    p.setParam(lp(0, SampleId), 0);                          // Air: a loop
    p.noteOn(60, 1.0);
    auto y = render(p, 4 * 48000).first;                     // longer than the loop (2 s)
    const double a = tu::rmsDb(y, 24000, 72000), b = tu::rmsDb(y, 120000, 168000);
    CHECK(a > -40.0);
    CHECK(std::abs(a - b) < 3.0);
    p.allSoundOff();
    // Knock: a one-shot; an octave up plays twice as fast, so it is over in half the time
    auto lenAt = [&](int key) {
        Processor q; typed(q, OscSample); q.setParam(lp(0, SampleId), 5); q.setParam(lp(0, AmpR), 2000);
        q.noteOn(key, 1.0);
        auto z = render(q, 48000).first;
        size_t last = 0; for (size_t i = 0; i < z.size(); ++i) if (std::abs(z[i]) > 1e-4) last = i;
        return static_cast<double>(last);
    };
    const double l60 = lenAt(60), l72 = lenAt(72);
    MESSAGE("knock: " << l60 / 48.0 << " ms at C4, " << l72 / 48.0 << " ms at C5");
    CHECK(l60 > 2000);
    CHECK(l72 / l60 == doctest::Approx(0.5).epsilon(0.05));
}

TEST_CASE("IN07 OSC: every sample level holds its band (content under 0.21 of its stored rate)") {
    const auto& sb = sampleBank();
    for (int id = 0; id < kSamples; ++id)
        for (int j = 0; j < SampleBank::kLevels; ++j) {
            const auto& v = sb.s[static_cast<size_t>(id)].lv[static_cast<size_t>(j)];
            double in = 0.0, out = 0.0;
            if (!sb.s[static_cast<size_t>(id)].loop) {   // a one-shot starts and ends at 0: the whole of it, zero-padded, no window
                int n = 1; while (n < static_cast<int>(v.size())) n *= 2;
                std::vector<std::complex<double>> x(static_cast<size_t>(n));
                for (size_t i = 0; i < v.size(); ++i) x[i] = v[i];
                sw::Fft f(n); f.forward(x);
                for (int b = 1; b < n / 2; ++b) (b < n / 4 ? in : out) += std::norm(x[static_cast<size_t>(b)]);
            }
            const int n = 4096;
            std::vector<std::complex<double>> x(static_cast<size_t>(n));
            for (size_t off = 8; sb.s[static_cast<size_t>(id)].loop && off + static_cast<size_t>(n) + 8 < v.size(); off += static_cast<size_t>(n)) {
                for (int i = 0; i < n; ++i) { const double t = 2 * tu::kPi * i / n; x[static_cast<size_t>(i)] = v[off + static_cast<size_t>(i)] * (0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t)); }
                sw::Fft f(n); f.forward(x);
                for (int b = 1; b < n / 2; ++b) (b < n / 4 ? in : out) += std::norm(x[static_cast<size_t>(b)]);
            }
            // the out-of-band share of this level's power, against the power of the sample itself (level 0): a level whose band holds
            // nothing of the sample (Tick's partials lie above level 4) is nearly empty, and its own ratio would compare two traces of noise
            auto ms = [](const std::vector<float>& u) { double a = 0; for (float q : u) a += static_cast<double>(q) * q; return a / static_cast<double>(u.size()); };
            const double db = 10 * std::log10(out / (in + out + 1e-30) * ms(v) / ms(sb.s[static_cast<size_t>(id)].lv[0]) + 1e-30);
            MESSAGE(std::string(kSampleNames[id]) << " level " << j << ": " << db << " dB above 0.25 of the stored rate");
            CHECK_MESSAGE(db < -60.0, std::string(kSampleNames[id]) << " level " << j);
        }
}

TEST_CASE("IN07 OSC: the sample read (8-tap windowed sinc) leaves no images at any speed") {
    // a multi-tone signal in the lower 0.21 of a 2x-oversampled buffer, read at increments 0.3 .. 3: everything off the tones is an image
    const int N = 200000;
    std::vector<float> buf(static_cast<size_t>(N));
    const double tones[5] = {0.013, 0.051, 0.097, 0.143, 0.205};   // cycles per stored sample
    for (int i = 0; i < N; ++i) { double y = 0; for (double t : tones) y += 0.2 * std::sin(2 * tu::kPi * t * i + t * 100); buf[static_cast<size_t>(i)] = static_cast<float>(y); }
    for (double inc : {0.3, 0.77, 1.0, 1.31, 2.0, 2.6, 3.0}) {
        const int n = 32768;
        std::vector<std::complex<double>> x(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            const double t = 2 * tu::kPi * i / n;
            x[static_cast<size_t>(i)] = sampleRead(buf.data(), 50.0 + i * inc) * (0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t));
        }
        sw::Fft f(n); f.forward(x);
        double on = 0, off = 0;
        for (int b = 1; b < n / 2; ++b) {
            const double fb = static_cast<double>(b) / n;   // cycles per output sample
            bool near = false;
            for (double t : tones) { double q = std::fmod(t * inc, 1.0); if (q > 0.5) q = 1.0 - q; near = near || std::abs(fb - q) < 6.0 / n; }
            (near ? on : off) += std::norm(x[static_cast<size_t>(b)]);
        }
        const double db = 10 * std::log10(off / on);
        MESSAGE("sample read at increment " << inc << ": " << db << " dB off the tones");
        CHECK(db < -60.0);
    }
}

TEST_CASE("IN07 OSC: modulation of the wavetable position and the FM index; gravity on a wavetable") {
    Processor p; typed(p, OscWavetable); p.setParam(lp(0, Table), 0); p.setParam(lp(0, Position), 0);
    p.setParam(modId(0, ModSrc), SrcModWheel); p.setParam(modId(0, ModDst), DstWtPos); p.setParam(modId(0, ModAmount), 100);
    p.modWheel(1.0);
    p.noteOn(57, 1.0); auto sq = render(p, 48000).first; p.allSoundOff();
    CHECK(tu::harmDb(sq, 220, 2) < -60.0);                   // the wheel moved it to the square end
    Processor q; typed(q, OscFm); q.setParam(lp(0, FmRatio), 5); q.setParam(lp(0, FmIndex), 0); q.setParam(lp(0, FmDecay), 10000);
    q.setParam(modId(0, ModSrc), SrcVelocity); q.setParam(modId(0, ModDst), DstFmIndex); q.setParam(modId(0, ModAmount), 10);   // velocity 1 = +1 rad
    q.noteOn(57, 1.0); auto y = render(q, 48000).first;
    CHECK(tu::harmDb(y, 220, 5) == doctest::Approx(20 * std::log10(0.4400506 / 0.7651977)).epsilon(0.01));
    Processor g; typed(g, OscWavetable); g.setParam(lp(0, Table), 3); g.setParam(lp(0, Position), 50);
    g.setParam(lp(0, Unison), 8); g.setParam(lp(0, Detune), 30); g.setParam(lp(0, Gravity), 100);
    g.noteOn(57, 1.0); render(g, 96000);
    CHECK(v0(g, 57).coherence() > 0.97);
}

TEST_CASE("IN07 OSC: the block size does not change any oscillator type") {
    auto play = [](int block) {
        Processor p; plain(p);
        p.setParam(lp(0, OscType), OscWavetable); p.setParam(lp(0, Table), 4); p.setParam(lp(0, Position), 40); p.setParam(lp(0, Unison), 3);
        plainLayer(p, 1); p.setParam(lp(1, OscType), OscFm); p.setParam(lp(1, FmRatio), 3); p.setParam(lp(1, FmIndex), 30); p.setParam(lp(1, FmDecay), 300); p.setParam(lp(1, FmFeedback), 40);
        plainLayer(p, 2); p.setParam(lp(2, OscType), OscSample); p.setParam(lp(2, SampleId), 2); p.setParam(lp(2, Unison), 2);
        p.noteOn(57, 0.8); p.noteOn(64, 0.5);
        auto a = render(p, 9000, block);
        p.noteOff(57);
        auto b = render(p, 9000, block);
        a.first.insert(a.first.end(), b.first.begin(), b.first.end());
        a.first.insert(a.first.end(), b.second.begin(), b.second.end());
        return a.first;
    };
    const auto x = play(256), y = play(37);
    REQUIRE(x.size() == y.size());
    size_t diff = 0; for (size_t i = 0; i < x.size(); ++i) if (x[i] != y[i]) ++diff;
    CHECK(diff == 0);
}

TEST_CASE("IN07: a layer whose decay reaches a sustain of 0 sleeps while the key is still held, and reports its end") {
    Processor p; plain(p);
    p.setParam(lp(0, AmpS), 0); p.setParam(lp(0, AmpD), 10);
    p.noteOn(60, 1.0, 0, 7);
    render(p, 240);
    CHECK(p.active());
    render(p, 2000);                                     // past the 10 ms decay
    CHECK_FALSE(p.active());
    int key = -1, ch = -1, id = -1;
    REQUIRE(p.takeEnded(key, ch, id));
    CHECK(key == 60); CHECK(id == 7);
    p.noteOff(60);                                       // the late note-off finds nothing to release
    CHECK_FALSE(p.takeEnded(key, ch, id));
}
