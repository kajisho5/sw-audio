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
    CHECK(kNumParams == kNumGlobal + kLayers * kLayerParams);
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
