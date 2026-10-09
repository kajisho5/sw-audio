#include "doctest.h"
#include "vo01/vo01.hpp"
#include "tu.hpp"
#include "voice.hpp"
using namespace sw;
using namespace sw::vo01;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> run1(Processor& p, std::vector<float> x) { return run(p, x); }
// the pitch (semitones) of a signal at every analysis hop, measured by a fresh analyser
std::vector<double> pitchTrack(const std::vector<float>& y) {
    PitchAnalyzer an; an.prepare(engineConfig(kFs)); std::vector<double> t;
    for (size_t i = 0; i < y.size(); ++i) { an.push(y[i]); if ((i % 128) == 127) t.push_back(an.currentVoiced() ? semis(an.currentF0()) : 0.0); }
    return t;
}
double meanSemis(const std::vector<double>& t, size_t a, size_t b) { double s = 0; int n = 0; for (size_t i = a; i < std::min(b, t.size()); ++i) if (t[i] > 0) { s += t[i]; ++n; } return n ? s / n : 0.0; }
double sdSemis(const std::vector<double>& t, size_t a, size_t b) { const double m = meanSemis(t, a, b); double s = 0; int n = 0; for (size_t i = a; i < std::min(b, t.size()); ++i) if (t[i] > 0) { s += (t[i] - m) * (t[i] - m); ++n; } return n ? std::sqrt(s / n) : 0.0; }
}

TEST_CASE("VO01 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"vo01.view", "vo01.scale", "vo01.speed", "vo01.humanize", "vo01.vibrato", "vo01.formant", "vo01.transpose", "vo01.detectmidi", "vo01.snap", "vo01.reference", "vo01.key", "vo01.customscale"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[View].labels == std::vector<std::string>{"Graph", "Auto"}); CHECK(s[View].def == 1);
    CHECK(s[Scale].labels == std::vector<std::string>{"Major", "Chromatic", "Custom"}); CHECK(s[Scale].def == 0);
    CHECK(s[Speed].min == 0); CHECK(s[Speed].max == 400); CHECK(s[Speed].def == 20); CHECK(s[Speed].curve == Curve::Skew); CHECK(s[Speed].skew == 2);
    CHECK(s[Humanize].def == 40); CHECK(s[Vibrato].labels == std::vector<std::string>{"Natural", "Reduce", "Flat"}); CHECK(s[Vibrato].def == 0);
    CHECK(s[Formant].labels == std::vector<std::string>{"Keep", "Follow"}); CHECK(s[Formant].def == 0);
    CHECK(s[Transpose].steps.size() == 25); CHECK(s[Transpose].def == 0);
    CHECK(s[DetectMidi].def == 0); CHECK(s[SnapToGrid].def == 1); CHECK(s[Reference].def == 0);
    CHECK(s[Key].labels.size() == 12); CHECK(s[Key].def == 0); CHECK(!s[CustomScale].automatable);
    // the bits of a scale
    CHECK(scaleBits(0, 0) == 0x0AB5); CHECK(scaleBits(2, 5) == 0x0FFF); CHECK(scaleBits(0, 2) == static_cast<uint16_t>(((0x0AB5 << 2) | (0x0AB5 >> 10)) & 0x0FFF));
}
TEST_CASE("VO01 reports its lookahead as the delay; silence is silence") {
    Processor q; CHECK(q.latencySamples() == PitchAnalyzer::latencyFor(engineConfig(kFs))); CHECK(q.latencySamples() > 1000); CHECK(q.latencySamples() < 1600);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run1(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("VO01 a note between two notes of the scale goes to the nearest one") {
    // 235 Hz = 58.1 semitones (MIDI): in C major the neighbours are A (57) and B (59): B
    auto p = make({{Speed, 0}, {Humanize, 0}, {Vibrato, Flat}});
    const auto y = run1(p, voice(235.0, 2.0));
    const auto t = pitchTrack(y);
    NEAR(meanSemis(t, 450, 700), 59.0, 0.08);
    // 226 Hz = 57.4 semitones: A
    auto q = make({{Speed, 0}, {Humanize, 0}, {Vibrato, Flat}}); const auto z = run1(q, voice(226.0, 2.0));
    NEAR(meanSemis(pitchTrack(z), 450, 700), 57.0, 0.08);
    // the level is kept
    NEAR(rmsDb(y, 60000, 90000), rmsDb(voice(235.0, 2.0), 60000, 90000), 1.0);
}
TEST_CASE("VO01 Chromatic goes to the nearest semitone, Key moves the scale") {
    auto p = make({{Scale, Chromatic}, {Speed, 0}, {Humanize, 0}, {Vibrato, Flat}});
    const auto y = run1(p, voice(228.0, 2.0));   // 57.8 semitones -> 58 (A#)
    NEAR(meanSemis(pitchTrack(y), 450, 700), 58.0, 0.08);
    // D major (key 2): 250 Hz = 59.4 semitones: between A# (not in D major) and B: B
    auto q = make({{Key, 2}, {Speed, 0}, {Humanize, 0}, {Vibrato, Flat}}); const auto z = run1(q, voice(250.0, 2.0));
    NEAR(meanSemis(pitchTrack(z), 450, 700), 59.0, 0.08);
    // Custom: only C and G (bits 0 and 7): 220 Hz (A, 57) goes to G (55) or C (60): G is 2 away, C 3
    auto r = make({{Scale, Custom}, {CustomScale, 1 + 128}, {Speed, 0}, {Humanize, 0}, {Vibrato, Flat}}); const auto w = run1(r, voice(220.0, 2.0));
    NEAR(meanSemis(pitchTrack(w), 450, 700), 55.0, 0.1);
}
TEST_CASE("VO01 Transpose moves the corrected pitch") {
    auto p = make({{Speed, 0}, {Humanize, 0}, {Vibrato, Flat}, {Transpose, 7}});
    const auto y = run1(p, voice(235.0, 2.0));
    NEAR(meanSemis(pitchTrack(y), 450, 700), 59.0 + 7.0, 0.1);
}
TEST_CASE("VO01 Speed: a slow correction takes its time") {
    // the voice sings A (220 Hz) for 1 s, then 240 Hz (58.2 semitones) for 1.5 s: B (59) is the note
    auto x = voice(220.0, 1.0); const auto x2 = voice(240.0, 1.5); x.insert(x.end(), x2.begin(), x2.end());
    auto fast = make({{Speed, 0}, {Humanize, 0}, {Vibrato, Flat}}); auto slow = make({{Speed, 300}, {Humanize, 0}, {Vibrato, Flat}});
    const auto tf = pitchTrack(run1(fast, x)), ts = pitchTrack(run1(slow, x));
    // the input steps at 1.0 s; the output at that time is 1450 samples (30 ms) later. 0.1 s after the step: the fast one is on B, the slow one has hardly moved
    const size_t at = static_cast<size_t>((1.0 + 0.1) * kFs / 128.0), later = static_cast<size_t>((1.0 + 1.2) * kFs / 128.0);
    NEAR(meanSemis(tf, at, at + 10), 59.0, 0.3);
    CHECK(meanSemis(ts, at, at + 10) < 58.6);
    NEAR(meanSemis(ts, later, later + 10), 59.0, 0.15);   // and it gets there
}
TEST_CASE("VO01 Vibrato: Natural keeps it, Reduce takes most of it, Flat all of it") {
    const auto x = vibratoVoice(220.0, 3.0, 0.6, 5.5);   // +-0.6 semitone around A
    auto sd = [&](double v) { auto p = make({{Speed, 0}, {Humanize, 0}, {Vibrato, v}}); const auto y = run1(p, x); return sdSemis(pitchTrack(y), 600, 1050); };
    const double nat = sd(Natural), red = sd(Reduce), flat = sd(Flat);
    CHECK(nat > 0.25); CHECK(red < 0.6 * nat); CHECK(flat < 0.35 * nat);
}
TEST_CASE("VO01 Humanize keeps part of the singer's offset from the note") {
    // 0.4 semitone sharp of A3 (57): 440 x 2^((57.4 - 69) / 12) = 225.1 Hz
    auto off = [&](double hum) { auto p = make({{Speed, 0}, {Humanize, hum}, {Vibrato, Flat}}); const auto y = run1(p, voice(440.0 * std::exp2((57.4 - 69.0) / 12.0), 2.0)); return meanSemis(pitchTrack(y), 450, 700) - 57.0; };
    const double h0 = off(0), h50 = off(50), h100 = off(100);
    NEAR(h0, 0.0, 0.08); NEAR(h50, 0.6 * 0.5 * 0.4, 0.08); NEAR(h100, 0.6 * 0.4, 0.08);
}
TEST_CASE("VO01 Formant Keep leaves the vowel where it is, Follow moves it with the pitch") {
    auto energyAround = [&](const std::vector<float>& y, double f) { double s = 0; for (double g = f * 0.85; g <= f * 1.15; g += 4.0) s += std::pow(10.0, binDb(y, g, 40000, 70000) / 10.0); return 10 * std::log10(s + 1e-30); };
    const auto x = voice(130.8, 1.8, 0.0, 700.0, 1800.0);   // C3
    auto keep = make({{Scale, Chromatic}, {Speed, 0}, {Transpose, 7}, {Formant, 0}, {Humanize, 0}, {Vibrato, Flat}}); auto follow = make({{Scale, Chromatic}, {Speed, 0}, {Transpose, 7}, {Formant, 1}, {Humanize, 0}, {Vibrato, Flat}});
    const auto yk = run1(keep, x), yf = run1(follow, x);
    const double r = std::exp2(7.0 / 12.0);
    CHECK(energyAround(yk, 700) > energyAround(yk, 700 * r) + 2.0);
    CHECK(energyAround(yf, 700 * r) > energyAround(yf, 700) + 2.0);
}
TEST_CASE("VO01 unvoiced sounds and Graph (no correction) pass; below 85 Hz nothing is shifted") {
    auto p = make({}); const auto n = noise(-20, 1.5, 3); const auto y = run1(p, n);
    NEAR(rmsDb(y, 40000, 70000), rmsDb(n, 40000, 70000), 1.0);
    auto g = make({{View, Graph}, {Speed, 0}}); const auto z = run1(g, voice(235.0, 2.0));
    NEAR(meanSemis(pitchTrack(z), 450, 700), 58.1, 0.12);   // not corrected
    // a stereo input comes out the same on both channels
    auto q = make(); std::vector<float> l = voice(235.0, 0.5), r = l; float* c[2] = {l.data(), r.data()}; q.process(c, 2, static_cast<int>(l.size())); for (size_t i = 0; i < l.size(); ++i) CHECK(l[i] == r[i]);
}
TEST_CASE("VO01 key detection suggests the key of what was sung") {
    // an E major melody: E F# G# A B C# D# E ... spread over some seconds
    const double notes[] = {64, 66, 68, 69, 71, 73, 75, 76, 71, 68, 64, 69, 73, 68, 64, 71};
    std::vector<float> x;
    for (double m : notes) { const auto v = voice(440.0 * std::exp2((m - 69.0) / 12.0), 0.5); x.insert(x.end(), v.begin(), v.end()); }
    auto p = make({}); run1(p, x);
    double conf = 0; CHECK(p.suggestedKey(&conf) == 4); CHECK(conf > 0.6);
    auto q = make({}); std::vector<float> z(48000, 0.0f); run1(q, z); double c2 = 1; CHECK(q.suggestedKey(&c2) == 0); CHECK(c2 == 0.0);
}
TEST_CASE("VO01 loud input at the extremes stays finite") {
    auto p = make({{Speed, 0}, {Transpose, 12}, {Formant, 1}}); auto x = voice(500.0, 1.0, 800.0, 700.0, 1800.0, 0.9); const auto y = run1(p, x); for (float v : y) { CHECK(std::isfinite(v)); CHECK(std::abs(v) < 8.0f); }
    auto q = make({{Transpose, -12}}); const auto z = run1(q, noise(0, 1.0, 5)); for (float v : z) { CHECK(std::isfinite(v)); CHECK(std::abs(v) < 8.0f); }
}

TEST_CASE("VO01 reports the singer's pitch and the corrected note for the screen") {
    // 235 Hz = 58.1 semitones: the singer's pitch 58.1, the corrected note B (59); silence is not voiced
    auto p = make({{Speed, 0}, {Humanize, 0}, {Vibrato, Flat}});
    run1(p, voice(235.0, 2.0));
    CHECK(p.voiced());
    NEAR(p.measuredSemitones(), 58.1, 0.15);
    NEAR(p.lastNoteSemitones(), 59.0, 0.15);
    auto q = make(); run1(q, std::vector<float>(48000, 0.0f));
    CHECK(!q.voiced());
}
