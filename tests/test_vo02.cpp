#include "doctest.h"
#include "vo02/vo02.hpp"
#include "tu.hpp"
#include "voice.hpp"
using namespace sw;
using namespace sw::vo02;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<double> pitchTrack(const std::vector<float>& y) {
    PitchAnalyzer an; an.prepare(engineConfig(kFs)); std::vector<double> t;
    for (size_t i = 0; i < y.size(); ++i) { an.push(y[i]); if ((i % 128) == 127) t.push_back(an.currentVoiced() ? semis(an.currentF0()) : 0.0); }
    return t;
}
double meanSemis(const std::vector<double>& t, size_t a, size_t b) { double s = 0; int n = 0; for (size_t i = a; i < std::min(b, t.size()); ++i) if (t[i] > 0) { s += t[i]; ++n; } return n ? s / n : 0.0; }
}

TEST_CASE("VO02 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"vo02.key", "vo02.scale", "vo02.speed", "vo02.humanize", "vo02.formant", "vo02.mix", "vo02.unit"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Key].labels.size() == 12); CHECK(s[Key].def == 0);
    CHECK(s[Scale].labels == std::vector<std::string>{"Maj", "Min", "Chr"}); CHECK(s[Scale].def == 0);
    CHECK(s[Speed].min == 0); CHECK(s[Speed].max == 100); CHECK(s[Speed].curve == Curve::Skew); CHECK(s[Speed].skew == 2); CHECK(s[Speed].reversed);
    NEAR(s[Speed].toValue(0.0), 100.0, 1e-9); NEAR(s[Speed].toValue(1.0), 0.0, 1e-9); NEAR(s[Speed].toValue(0.5), 25.0, 1e-9); NEAR(s[Speed].def, 25.0, 1e-9);   // Slow .. Hard = 100 .. 0 ms, the middle 25 ms
    CHECK(s[Humanize].max == 10); CHECK(s[Humanize].def == 3); CHECK(s[Formant].min == -3); CHECK(s[Formant].max == 3); CHECK(s[Formant].def == 0); CHECK(s[Mix].def == 100);
}
TEST_CASE("VO02 reports its lookahead; silence is silence") {
    Processor q; CHECK(q.latencySamples() == PitchAnalyzer::latencyFor(engineConfig(kFs))); CHECK(q.latencySamples() < 1200); CHECK(q.latencySamples() > 900);
    CHECK(q.latencySamples() < PitchAnalyzer::latencyFor(sw::PitchConfig{}));   // shorter than the VO01 setting
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("VO02 pulls a note to the scale; Min and Chr; Key") {
    auto at = [&](Set set, double hz) { auto p = make(set); const auto y = run(p, voice(hz, 2.0)); return meanSemis(pitchTrack(y), 450, 700); };
    NEAR(at({{Speed, 0}, {Humanize, 0}}, 235.0), 59.0, 0.12);                     // C major: B
    NEAR(at({{Speed, 0}, {Humanize, 0}, {Scale, Chr}}, 228.0), 58.0, 0.12);        // chromatic: A#
    // C minor (key 0, Min): notes C D D# F G G# A#: 245 Hz is 58.8 semitones -> A# (58) is 0.8 away, C (60) 1.2 away: A#
    NEAR(at({{Speed, 0}, {Humanize, 0}, {Scale, Min}}, 245.0), 58.0, 0.12);
    NEAR(at({{Speed, 0}, {Humanize, 0}, {Key, 2}}, 250.0), 59.0, 0.12);           // D major: B
}
TEST_CASE("VO02 Speed: Hard is quick, Slow takes about 100 ms to settle") {
    auto x = voice(220.0, 1.0); const auto x2 = voice(240.0, 1.5); x.insert(x.end(), x2.begin(), x2.end());
    auto hard = make({{Speed, 0}, {Humanize, 0}}); auto slow = make({{Speed, 100}, {Humanize, 0}});
    const auto th = pitchTrack(run(hard, x)), ts = pitchTrack(run(slow, x));
    const size_t at = static_cast<size_t>((1.0 + 0.12) * kFs / 128.0), later = static_cast<size_t>((1.0 + 1.0) * kFs / 128.0);
    NEAR(meanSemis(th, at, at + 10), 59.0, 0.3);
    NEAR(meanSemis(ts, later, later + 10), 59.0, 0.2);
}
TEST_CASE("VO02 Formant moves the vowel, not the pitch") {
    auto energyAround = [&](const std::vector<float>& y, double f) { double s = 0; for (double g = f * 0.85; g <= f * 1.15; g += 4.0) s += std::pow(10.0, binDb(y, g, 40000, 70000) / 10.0); return 10 * std::log10(s + 1e-30); };
    const auto x = voice(130.8, 1.8, 0.0, 700.0, 1800.0);
    auto flat = make({{Scale, Chr}, {Speed, 0}, {Humanize, 0}, {Formant, 0}}); auto up = make({{Scale, Chr}, {Speed, 0}, {Humanize, 0}, {Formant, 3}});
    const auto yf = run(flat, x), yu = run(up, x);
    NEAR(peakHz(yf, 120, 145, 40000, 70000), peakHz(yu, 120, 145, 40000, 70000), 1.5);      // the pitch is the same
    const double r = std::exp2(3.0 / 12.0);
    CHECK(energyAround(yu, 700 * r) > energyAround(yf, 700 * r) + 1.0);
    CHECK(energyAround(yf, 700) > energyAround(yu, 700) + 1.0);
}
TEST_CASE("VO02 an unstable stretch is corrected less") {
    // the voice jumps by 4 semitones between two periods (a wrong estimate looks like that): the corrector must not follow at full strength
    PitchCorrector c; PitchCorrector::Settings s; s.speedMs = 0; s.humanize = 0; s.weakenWhenUnstable = true; c.setSettings(s);
    double r, fm; const double dt = 0.004;
    for (int k = 0; k < 40; ++k) c.ratio(440.0 * std::exp2((57.2 - 69.0) / 12.0), true, dt, r, fm);   // steady, 0.2 semitone sharp
    const double steady = std::log2(r) * 12.0;      // about -0.2: the full correction
    c.ratio(440.0 * std::exp2((61.2 - 69.0) / 12.0), true, dt, r, fm);                                 // a jump
    const double jump = std::log2(r) * 12.0;
    NEAR(steady, -0.2, 0.05);
    // the same jump without the weakening is corrected in full: the note is D (62) for 61.2: +0.8 semitone
    PitchCorrector d; PitchCorrector::Settings s2; s2.speedMs = 0; s2.humanize = 0; s2.weakenWhenUnstable = false; d.setSettings(s2);
    for (int k = 0; k < 40; ++k) d.ratio(440.0 * std::exp2((57.2 - 69.0) / 12.0), true, dt, r, fm);
    d.ratio(440.0 * std::exp2((61.2 - 69.0) / 12.0), true, dt, r, fm);
    const double full = std::log2(r) * 12.0;
    NEAR(full, 0.8, 0.05); NEAR(jump, 0.3 * 0.8, 0.05);
}
TEST_CASE("VO02 loud input stays finite; a stereo input comes out the same on both sides") {
    auto p = make({{Speed, 0}, {Formant, 3}}); const auto y = run(p, voice(500.0, 1.0, 800.0, 700.0, 1800.0, 0.9)); for (float v : y) { CHECK(std::isfinite(v)); CHECK(std::abs(v) < 8.0f); }
    auto q = make(); std::vector<float> l = voice(235.0, 0.5), r = l; float* c[2] = {l.data(), r.data()}; q.process(c, 2, static_cast<int>(l.size())); for (size_t i = 0; i < l.size(); ++i) CHECK(l[i] == r[i]);
}
