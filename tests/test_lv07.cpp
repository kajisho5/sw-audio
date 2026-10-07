#include "doctest.h"
#include "lv07/lv07.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv07;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// words: harmonic voice (f0 140 Hz, 12 harmonics, each lowered by `tilt` dB per octave) on for 0.5 s, off for `gap` s; level lvl dBFS rms in the words; room noise `room` dBFS in the gaps and under the words
std::vector<float> talk(double seconds, double lvl, double room, double tilt, double gap, double roomReverb = 0) {
    const size_t n = static_cast<size_t>(seconds * kFs); std::vector<float> y(n); double ph = 0; Gauss g(5); double lp = 0; const double ra = std::pow(10.0, room / 20.0);
    double sumSq = 0; for (int h = 1; h <= 12; ++h) { const double a = std::pow(10.0, -tilt * std::log2(h) / 20.0) / h; sumSq += a * a / 2; }
    const double a0 = std::pow(10.0, lvl / 20.0) / std::sqrt(sumSq);
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kFs, cyc = std::fmod(t, 0.5 + gap); const bool on = cyc < 0.5; ph += 2 * kPi * 140.0 * (1 + 0.03 * std::sin(2 * kPi * 4 * t)) / kFs;
        double s = 0; if (on) for (int h = 1; h <= 12; ++h) s += std::pow(10.0, -tilt * std::log2(h) / 20.0) / h * std::sin(h * ph);
        lp = 0.9 * lp + 0.1 * g.gauss();
        y[i] = static_cast<float>(a0 * s + ra * (roomReverb > 0 ? lp * 3.0 : g.gauss()) + (on ? 0.0 : 0.0));
    }
    return y;
}
}

TEST_CASE("LV07 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Use].labels == std::vector<std::string>{"Speech", "Panel", "Lecture"}); CHECK(s[Use].def == 0);
    CHECK(s[Target].min == -30); CHECK(s[Target].max == -10); CHECK(s[Target].def == -18);
    CHECK(s[MaxGain].min == 0); CHECK(s[MaxGain].max == 24); CHECK(s[MaxGain].def == 12);
    CHECK(s[Speed].labels == std::vector<std::string>{"Slow", "Medium", "Fast"}); CHECK(s[Speed].def == 1);
    CHECK(s[Gate].min == -70); CHECK(s[Gate].max == -30); CHECK(s[Gate].def == -50);
    CHECK(s[TalkerHold].def == 1); CHECK(s[Freeze].def == 0);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV07 levels a quiet and a loud talker to the target") {
    double outL[2];
    int k = 0;
    for (double lvl : {-28.0, -10.0}) {
        auto p = make({{Speed, 2}, {MaxGain, 24}}); const auto y = run(p, talk(60.0, lvl, -75, 6, 0.3));
        IntegratedLoudness m; m.setup(kFs, 2, 0.0); const std::vector<float> tail(y.begin() + 30 * 48000, y.end()); const float* c[2] = {tail.data(), tail.data()}; m.process(c, 2, static_cast<int>(tail.size()));
        outL[k++] = m.integrated(); CHECK(std::abs(outL[k - 1] - (-18.0)) < 3.0);
    }
    CHECK(std::abs(outL[0] - outL[1]) < 4.0);
    auto q = make({{Speed, 2}}); run(q, talk(40.0, -34, -75, 6, 0.3)); CHECK(q.gainDb() > 5.0);
    auto r = make({{Speed, 2}}); run(r, talk(40.0, -10, -75, 6, 0.3)); CHECK(r.gainDb() < -2.0);
}
TEST_CASE("LV07 Max gain, Speed and Use change how far and how fast") {
    auto a = make({{Speed, 2}, {MaxGain, 4}}); run(a, talk(30.0, -45, -75, 6, 0.3)); CHECK(a.gainDb() <= 4.0 + 3.0 * a.distance() + 1e-9);
    auto s = make({{Speed, 0}}); run(s, talk(6.0, -45, -75, 6, 0.3)); auto f = make({{Speed, 2}}); run(f, talk(6.0, -45, -75, 6, 0.3)); CHECK(f.gainDb() > s.gainDb() + 3.0);
    auto pn = make({{Speed, 0}, {Use, Panel}}); run(pn, talk(6.0, -45, -75, 6, 0.3)); auto lc = make({{Speed, 0}, {Use, Lecture}}); run(lc, talk(6.0, -45, -75, 6, 0.3)); CHECK(pn.gainDb() > lc.gainDb() + 1.0);
}
TEST_CASE("LV07 Gate, Talker hold and Freeze") {
    const auto words = talk(20.0, -30, -75, 6, 0.3);
    auto h = make({{Speed, 2}}); run(h, words); const double g = h.gainDb(); run(h, noise(-70, 10.0, 3)); CHECK(std::abs(h.gainDb() - g) < 0.1);
    auto o = make({{Speed, 2}, {TalkerHold, 0}}); run(o, words); run(o, noise(-70, 10.0, 3)); CHECK(o.gainDb() < g - 3.0);
    auto f = make({{Speed, 2}, {Freeze, 1}}); run(f, words); CHECK(f.gainDb() == 0.0);
    auto gt = make({{Speed, 2}, {Gate, -30}}); run(gt, talk(20.0, -38, -75, 6, 0.3)); CHECK(gt.gainDb() == 0.0);   // everything is under the gate
}
TEST_CASE("LV07 near / far: a roomy, dull talker is further away than a close, bright one") {
    auto near = make({{Speed, 2}}); run(near, talk(30.0, -30, -80, 3, 0.4));
    auto far = make({{Speed, 2}}); run(far, talk(30.0, -30, -38, 12, 0.4, 1.0));
    CHECK(far.distance() > near.distance() + 0.3);
    CHECK(near.distance() < 0.4);
}
TEST_CASE("LV07 limiter at -1 dBFS; mono, odd blocks, before prepare, silence") {
    auto p = make({{Speed, 2}, {Target, -10}, {MaxGain, 24}}); const auto x = talk(20.0, -12, -75, 6, 0.3); for (float v : run(p, x)) { REQUIRE(std::isfinite(v)); REQUIRE(std::abs(v) <= 0.89126f + 1e-6f); }
    auto m = make(); std::vector<float> l = noise(-20, 2.0, 5); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; m.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f);
    auto q = make(); for (float v : run(q, std::vector<float>(48000, 0.0f))) REQUIRE(v == 0.0f);
}
