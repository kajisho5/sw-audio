#include "doctest.h"
#include "lv10/lv10.hpp"
#include "tu.hpp"
#include "voice.hpp"
using namespace sw;
using namespace sw::lv10;
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

TEST_CASE("LV10 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Preset].labels == std::vector<std::string>{"None", "Low", "High", "Robot", "Radio", "Anon"}); CHECK(s[Preset].def == 0);
    CHECK(s[Pitch].min == -12); CHECK(s[Pitch].max == 12); CHECK(s[Pitch].def == -3);
    CHECK(s[Formant].min == -5); CHECK(s[Formant].max == 5); CHECK(s[Formant].def == 2);
    CHECK(s[Robot].def == 0); CHECK(s[Mix].def == 100); CHECK(s[Monitor].def == 1);
}
TEST_CASE("LV10 reports the real delay of the engine; silence is silence") {
    Processor q; CHECK(q.latencySamples() == PitchAnalyzer::latencyFor(engineConfig(kFs))); CHECK(q.latencySamples() > 900); CHECK(q.latencySamples() < 1200);
    auto p = make(); for (float v : run(p, std::vector<float>(48000, 0.0f))) CHECK(v == 0.0f);
}
TEST_CASE("LV10 Pitch moves the voice by that many semitones") {
    const auto x = voice(220.0, 2.0);
    for (double st : {-5.0, 3.0, 7.0}) { auto p = make({{Pitch, st}, {Formant, 0}}); const auto y = run(p, x); NEAR(meanSemis(pitchTrack(y), 450, 700), semis(220.0) + st, 0.25); }
}
TEST_CASE("LV10 Formant moves the vowel, not the pitch") {
    auto energyAround = [&](const std::vector<float>& y, double f) { double s = 0; for (double g = f * 0.85; g <= f * 1.15; g += 4.0) s += std::pow(10.0, binDb(y, g, 40000, 70000) / 10.0); return 10 * std::log10(s + 1e-30); };
    const auto x = voice(130.8, 1.8, 0.0, 700.0, 1800.0);
    auto flat = make({{Pitch, 0}, {Formant, 0}}); auto up = make({{Pitch, 0}, {Formant, 4}}); const auto yf = run(flat, x), yu = run(up, x); const double r = std::exp2(4.0 / 12.0);
    CHECK(energyAround(yu, 700 * r) > energyAround(yf, 700 * r) + 1.0); CHECK(energyAround(yf, 700) > energyAround(yu, 700) + 1.0);
}
TEST_CASE("LV10 Robot holds the pitch whatever is spoken") {
    auto x = voice(200.0, 1.0, 260.0);   // glides 200 -> 260 Hz
    auto p = make({{Pitch, 0}, {Formant, 0}, {Robot, 1}}); const auto y = run(p, x); const auto t = pitchTrack(y);
    const double a = meanSemis(t, 160, 220), b = meanSemis(t, 300, 360);   // two moments of the glide
    CHECK(std::abs(a - b) < 1.0);
    auto q = make({{Pitch, 0}, {Formant, 0}, {Robot, 0}}); const auto t2 = pitchTrack(run(q, x)); CHECK(meanSemis(t2, 300, 360) - meanSemis(t2, 160, 220) > 1.5);
}
TEST_CASE("LV10 Preset writes Pitch, Formant and Robot; a value set afterwards cancels its write") {
    Processor p; p.prepare(kFs, 256); p.setParam(Preset, High); int n = 0, id; double v; bool sawPitch = false;
    while (p.takeParamWrite(id, v)) { ++n; if (id == Pitch) { sawPitch = true; CHECK(v == 5); } REQUIRE((id == Pitch || id == Formant || id == Robot)); }
    CHECK(n == 3); CHECK(sawPitch);
    Processor q; q.prepare(kFs, 256); q.setParam(Preset, Anon); q.setParam(Pitch, -7.0); n = 0; while (q.takeParamWrite(id, v)) { ++n; CHECK(id != Pitch); } CHECK(n == 2);
    Processor z; z.prepare(kFs, 256); z.setParam(Preset, None); CHECK_FALSE(z.takeParamWrite(id, v));
    CHECK(presetValues(Anon)[0].second == -3); CHECK(presetValues(Anon)[1].second == 2);
}
TEST_CASE("LV10 Radio limits the band; Anon changes the voice but stays finite") {
    auto p = make({{Preset, Radio}, {Pitch, 0}, {Formant, 0}}); const auto y = run(p, voice(180.0, 2.0)); const auto x = voice(180.0, 2.0);
    CHECK(binDb(y, 180, 40000, 80000) < binDb(x, 180, 40000, 80000) - 8.0);   // 180 Hz is under the 300 Hz high-pass (the voice's higher harmonics get through)
    auto a = make({{Preset, Anon}}); const auto z = run(a, voice(180.0, 3.0)); for (float v : z) REQUIRE(std::isfinite(v)); CHECK(rmsDb(z, 60000, 140000) > -40.0);
}
TEST_CASE("LV10 Monitor Off passes the voice untouched; mono; odd blocks; before prepare") {
    const auto x = voice(200.0, 1.0); auto p = make({{Monitor, 0}}); const auto y = run(p, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]); CHECK_FALSE(p.monitorOn());
    auto m = make(); std::vector<float> l = voice(200.0, 1.5); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; m.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f);
}
