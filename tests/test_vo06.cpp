#include "doctest.h"
#include "vo06/vo06.hpp"
#include "voice.hpp"
using namespace sw;
using namespace sw::vo06;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
double energyAround(const std::vector<float>& y, double f, size_t a = 40000, size_t b = 70000) { double s = 0; for (double g = f * 0.85; g <= f * 1.15; g += 4.0) s += std::pow(10.0, binDb(y, g, a, b) / 10.0); return 10 * std::log10(s + 1e-30); }
}

TEST_CASE("VO06 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"vo06.pitch", "vo06.formant", "vo06.character", "vo06.keeptiming", "vo06.smooth", "vo06.mix"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Pitch].min == -12); CHECK(s[Pitch].max == 12); CHECK(s[Pitch].def == 0);
    CHECK(s[Formant].min == -5); CHECK(s[Formant].max == 5); CHECK(s[Formant].def == 0);
    CHECK(s[Character].labels == std::vector<std::string>{"Neutral", "Deep", "Bright", "Child"}); CHECK(s[Character].def == 0);
    CHECK(s[KeepTiming].labels == std::vector<std::string>{"Off", "On"}); CHECK(s[KeepTiming].def == 1);
    CHECK(s[Smooth].labels == std::vector<std::string>{"Off", "On"}); CHECK(s[Smooth].def == 0);
    CHECK(s[Mix].def == 100);
}
TEST_CASE("VO06 reports its lookahead; silence is silence") {
    Processor q; CHECK(q.latencySamples() == PitchAnalyzer::latencyFor(engineConfig(kFs)));
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("VO06 Pitch moves the fundamental and keeps the timing") {
    const auto x = voice(130.8, 1.8);
    for (double st : {-12.0, -5.0, 7.0, 12.0}) {
        auto p = make({{Pitch, st}}); const auto y = run(p, x);
        const double f = 130.8 * std::exp2(st / 12.0);
        NEAR(peakHz(y, f * 0.93, f * 1.07, 40000, 70000), f, f * 0.012);
    }
    // timing: an impulse-like onset comes out at the same place (after the reported delay) with and without a shift
    auto onset = [&](double st) {
        std::vector<float> in(48000, 0.0f); const auto v = voice(130.8, 0.5); for (size_t i = 0; i < v.size(); ++i) in[12000 + i] = v[i];
        auto p = make({{Pitch, st}}); const auto y = run(p, in); const size_t lat = static_cast<size_t>(p.latencySamples());
        size_t k = 0; while (k < y.size() && std::abs(y[k]) < 0.05f) ++k; return static_cast<double>(k) - static_cast<double>(lat);
    };
    NEAR(onset(5.0), onset(0.0), 200.0);
}
TEST_CASE("VO06 Neutral with no shift keeps the voice (same pitch, same vowel, level)") {
    const auto x = voice(130.8, 1.8); auto p = make(); const auto y = run(p, x);
    NEAR(peakHz(y, 120, 145, 40000, 70000), 130.8, 1.5);
    NEAR(rmsDb(y, 40000, 70000), rmsDb(x, 40000 - static_cast<size_t>(p.latencySamples()), 70000 - static_cast<size_t>(p.latencySamples())), 1.0);
}
TEST_CASE("VO06 Formant moves the vowel, not the pitch") {
    const auto x = voice(130.8, 1.8, 0.0, 700.0, 1800.0);
    auto flat = make(); auto up = make({{Formant, 4}}); auto dn = make({{Formant, -4}});
    const auto yf = run(flat, x), yu = run(up, x), yd = run(dn, x);
    NEAR(peakHz(yu, 120, 145, 40000, 70000), peakHz(yf, 120, 145, 40000, 70000), 1.5);
    NEAR(peakHz(yd, 120, 145, 40000, 70000), peakHz(yf, 120, 145, 40000, 70000), 1.5);
    const double r = std::exp2(4.0 / 12.0);
    CHECK(energyAround(yu, 700 * r) > energyAround(yf, 700 * r) + 1.5);
    CHECK(energyAround(yf, 700) > energyAround(yu, 700) + 1.5);
    CHECK(energyAround(yd, 700 / r) > energyAround(yf, 700 / r) + 1.5);
}
TEST_CASE("VO06 Character adds to the two knobs: Deep lowers the pitch and the vowel, Child raises both") {
    const auto d = characterSemis(Deep), c = characterSemis(Child), b = characterSemis(Bright), n = characterSemis(Neutral);
    CHECK(n[0] == 0.0); CHECK(n[1] == 0.0); CHECK(d[0] < 0.0); CHECK(d[1] < 0.0); CHECK(c[0] > 0.0); CHECK(c[1] > 0.0); CHECK(b[0] == 0.0); CHECK(b[1] > 0.0);
    const auto x = voice(130.8, 1.8);
    auto pd = make({{Character, Deep}}); auto pc = make({{Character, Child}});
    const auto yd = run(pd, x), yc = run(pc, x);
    NEAR(peakHz(yd, 100, 140, 40000, 70000), 130.8 * std::exp2(d[0] / 12.0), 1.5);
    NEAR(peakHz(yc, 150, 200, 40000, 70000), 130.8 * std::exp2(c[0] / 12.0), 2.0);
    // the character adds to the knob
    auto pk = make({{Character, Child}, {Pitch, -4}}); const auto yk = run(pk, x);
    NEAR(peakHz(yk, 110, 150, 40000, 70000), 130.8 * std::exp2((c[0] - 4.0) / 12.0), 1.5);
}
TEST_CASE("VO06 Keep timing Off: the formant follows the pitch") {
    const auto x = voice(130.8, 1.8, 0.0, 700.0, 1800.0);
    auto on = make({{Pitch, 5}}); auto off = make({{Pitch, 5}, {KeepTiming, 0}});
    const auto yon = run(on, x), yoff = run(off, x);
    const double r = std::exp2(5.0 / 12.0);
    CHECK(energyAround(yoff, 700 * r) > energyAround(yon, 700 * r) + 1.5);    // the vowel moved up with the pitch
    CHECK(energyAround(yon, 700) > energyAround(yoff, 700) + 1.5);
    NEAR(peakHz(yoff, 160, 200, 40000, 70000), peakHz(yon, 160, 200, 40000, 70000), 1.5);   // the pitch is the same
}
TEST_CASE("VO06 Smooth: a change of setting glides instead of jumping") {
    auto cross = [&](double smooth) {
        auto p = make({{Pitch, 0}, {Smooth, smooth}}); const auto x = voice(130.8, 3.0);
        std::vector<float> l = x, r = x; bool changed = false;
        for (size_t off = 0; off < l.size(); off += 256) {
            if (off >= 48000 && !changed) { p.setParam(Pitch, 12); changed = true; }
            const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n);
        }
        // 50 ms of output just after the change reaches the output: the jump is already an octave up, the 40 ms glide is still part-way
        const size_t at = 48000 + 512;
        std::vector<float> w(l.begin() + static_cast<long>(at), l.begin() + static_cast<long>(at) + 2400);
        return peakHz(w, 120, 280, 0, w.size());
    };
    const double jump = cross(0.0), glide = cross(1.0);
    CHECK(jump > glide);   // the window after the change: the glide is still part-way (lower), the jump is nearer the octave
}
TEST_CASE("VO06 loud input stays finite; a stereo input comes out the same on both sides") {
    auto p = make({{Pitch, 7}, {Formant, 5}}); const auto y = run(p, voice(500.0, 1.0, 800.0, 700.0, 1800.0, 0.9)); for (float v : y) { CHECK(std::isfinite(v)); CHECK(std::abs(v) < 8.0f); }
    auto q = make(); std::vector<float> l = voice(235.0, 0.5), r = l; float* c[2] = {l.data(), r.data()}; q.process(c, 2, static_cast<int>(l.size())); for (size_t i = 0; i < l.size(); ++i) CHECK(l[i] == r[i]);
}
