#include "doctest.h"
#include "lv18/lv18.hpp"
#include "tu.hpp"
#include "voice.hpp"
using namespace sw;
using namespace sw::lv18;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> talk(double seconds) { return voice(180.0, seconds, 0.0, 700.0, 1800.0, 0.05); }
// add an event at sample `at`
void addPlug(std::vector<float>& x, size_t at) { for (size_t i = 0; i < 4800 && at + i < x.size(); ++i) x[at + i] += 0.4f * std::exp(-static_cast<float>(i) / 1500.0f); }
void addKnock(std::vector<float>& x, size_t at) { for (size_t i = 0; i < 4800 && at + i < x.size(); ++i) x[at + i] += 0.8f * std::exp(-static_cast<float>(i) / 1200.0f) * static_cast<float>(std::sin(2 * kPi * 90.0 * static_cast<double>(i) / kFs)); }
void addPuff(std::vector<float>& x, size_t at) { for (size_t i = 0; i < 2400 && at + i < x.size(); ++i) x[at + i] += 0.3f * static_cast<float>(std::sin(kPi * static_cast<double>(i) / 2400.0) * std::sin(2 * kPi * 70.0 * static_cast<double>(i) / kFs)); }
double peakIn(const std::vector<float>& y, size_t a, size_t b) { double p = 0; for (size_t i = a; i < b && i < y.size(); ++i) p = std::max(p, static_cast<double>(std::abs(y[i]))); return p; }
}

TEST_CASE("LV18 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Sensitivity].labels == std::vector<std::string>{"Low", "Mid", "High"}); CHECK(s[Sensitivity].def == 2);
    CHECK(s[MuteTime].min == 10); CHECK(s[MuteTime].max == 200); CHECK(s[MuteTime].def == 40); CHECK(s[MuteTime].curve == Curve::Log);
    CHECK(s[PlugPop].def == 1); CHECK(s[Wind].def == 1); CHECK(s[Handling].def == 1); CHECK(s[Plosive].def == 0);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV18 speech alone: nothing caught, bit for bit") {
    auto p = make({{Plosive, 1}}); const auto x = talk(4.0), y = run(p, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]); CHECK(p.caughtTotal() == 0);
}
TEST_CASE("LV18 plug pop: the thump is cut, the voice around it stays") {
    auto x = talk(4.0); addPlug(x, 96000); auto p = make({{Plosive, 0}}); const auto y = run(p, x);
    CHECK(p.caught(EvPlug) == 1); CHECK(peakIn(y, 96000 + 480, 96000 + 4800) < 0.5 * peakIn(x, 96000 + 480, 96000 + 4800));
    NEAR(y[48000], x[48000], 1e-6); NEAR(y[170000], x[170000], 1e-6);   // far from the event: untouched
    auto off = make({{PlugPop, 0}, {Handling, 0}, {Wind, 0}}); const auto z = run(off, x); CHECK(off.caughtTotal() == 0); for (size_t i = 96000; i < 96100; ++i) REQUIRE(z[i] == x[i]);
}
TEST_CASE("LV18 handling: a low knock is muted for Mute time") {
    auto x = talk(4.0); addKnock(x, 96000); auto p = make({{MuteTime, 100}, {PlugPop, 0}}); const auto y = run(p, x);
    CHECK(p.caught(EvHandling) >= 1); CHECK(peakIn(y, 96000 + 960, 96000 + 4800) < 0.4 * peakIn(x, 96000 + 960, 96000 + 4800));
    NEAR(y[40000], x[40000], 1e-6);
}
TEST_CASE("LV18 plosive: only when it is on; the puff loses its low end") {
    auto x = talk(4.0); addPuff(x, 96000);
    auto off = make(); run(off, x); CHECK(off.caught(EvPlosive) == 0);
    auto on = make({{Plosive, 1}, {MuteTime, 100}, {Handling, 0}, {PlugPop, 0}}); const auto y = run(on, x); CHECK(on.caught(EvPlosive) >= 1);
    CHECK(binDb(y, 70, 96000 + 600, 96000 + 2400) < binDb(x, 70, 96000 + 600, 96000 + 2400) - 6.0);
}
TEST_CASE("LV18 wind: steady rumble is high-passed while it lasts, then released") {
    std::vector<float> x(static_cast<size_t>(5 * kFs), 0.0f); const auto talkv = talk(5.0); auto rum = noise(-14, 5.0, 3); double lp = 0; for (auto& v : rum) { lp += 0.01 * (v - lp); v = static_cast<float>(lp * 7.0); }
    for (size_t i = 0; i < x.size(); ++i) x[i] = talkv[i] + (i >= 48000 && i < 3 * 48000 ? rum[i] : 0.0f);
    auto p = make(); const auto y = run(p, x); CHECK(p.caught(EvWind) == 1);
    CHECK(rmsDb(y, 2 * 48000, 3 * 48000) < rmsDb(x, 2 * 48000, 3 * 48000) - 6.0);   // the rumble is gone
    NEAR(y[4 * 48000 + 5000], x[4 * 48000 + 5000], 0.01);   // after the wind: the voice is back through (the high-pass has faded out)
    auto off = make({{Wind, 0}}); run(off, x); CHECK(off.caught(EvWind) == 0);
}
TEST_CASE("LV18 Sensitivity and counters") {
    auto weakPlug = talk(4.0); for (size_t i = 0; i < 4800; ++i) weakPlug[96000 + i] += 0.06f * std::exp(-static_cast<float>(i) / 1500.0f);
    auto hi = make({{Sensitivity, 2}, {Handling, 0}, {Wind, 0}}); run(hi, weakPlug); auto lo = make({{Sensitivity, 0}, {Handling, 0}, {Wind, 0}}); run(lo, weakPlug); CHECK(hi.caughtTotal() >= 1); CHECK(lo.caughtTotal() == 0);
    hi.resetCounts(); CHECK(hi.caughtTotal() == 0);
}
TEST_CASE("LV18 mono, odd blocks, loud noise, silence, before prepare") {
    auto p = make(); std::vector<float> l = talk(1.5); addKnock(l, 20000); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; p.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    auto q = make(); for (float v : run(q, std::vector<float>(48000, 0.0f))) REQUIRE(v == 0.0f);
    auto r = make(); for (float v : run(r, noise(6, 2.0, 3))) REQUIRE(std::isfinite(v));
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f);
}
