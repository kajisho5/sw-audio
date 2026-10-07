#include "doctest.h"
#include "ut02/ut02.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::ut02;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); return p; }
double gainAt(Set set, double hz) { auto p = make(set); const auto y = run(p, sine(-24, 1.0, hz)); return rmsDb(y, 24000, 48000) + 24.0; }
}

TEST_CASE("UT02 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"ut02.listen", "ut02.fold", "ut02.phone", "ut02.lowcut", "ut02.level"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Listen].labels == std::vector<std::string>{"Stereo", "Mono", "Side", "Left", "Right"}); CHECK(s[Listen].def == 1);
    CHECK(s[MonoFold].min == -6); CHECK(s[MonoFold].max == 0); CHECK(s[MonoFold].def == -3);
    CHECK(s[Phone].def == 0); CHECK(s[LowCut].min == 20); CHECK(s[LowCut].max == 300); CHECK(s[LowCut].curve == Curve::Log); CHECK(std::string(s[LowCut].minLabel) == "Off");
    CHECK(s[Level].min == -24); CHECK(s[Level].max == 24); CHECK(s[Level].def == 0);
}
TEST_CASE("UT02 no delay; Stereo is untouched; the export warning") {
    Processor q; CHECK(q.latencySamples() == 0);
    const auto a = noise(-18, 1.0, 1), b = noise(-18, 1.0, 2);
    auto p = make({{Listen, Stereo}}); const auto r = run2(p, a, b); for (size_t i = 0; i < a.size(); ++i) { CHECK(r.first[i] == a[i]); CHECK(r.second[i] == b[i]); }
    CHECK_FALSE(p.exportWarning());
    for (Set s : {Set{{Listen, Mono}}, Set{{Listen, Side}}, Set{{Phone, 1}}, Set{{LowCut, 100}}}) { auto w = make(s); CHECK(w.exportWarning()); }
}
TEST_CASE("UT02 Mono, Side, Left, Right") {
    const auto a = noise(-18, 1.0, 1), b = noise(-18, 1.0, 2);
    { auto p = make({{Listen, Mono}, {MonoFold, -3}}); const auto r = run2(p, a, b); for (size_t i = 0; i < a.size(); i += 17) { NEAR(r.first[i], (a[i] + b[i]) * 0.70794578, 1e-6); CHECK(r.first[i] == r.second[i]); } }
    { auto p = make({{Listen, Mono}, {MonoFold, -6}}); const auto r = run2(p, a, a); for (size_t i = 0; i < a.size(); i += 17) NEAR(r.first[i], a[i] * 1.0023745, 5e-3 * std::abs(a[i]) + 1e-6); }   // -6 dB: about the mean
    { auto p = make({{Listen, Side}}); const auto r = run2(p, a, b); for (size_t i = 0; i < a.size(); i += 17) { NEAR(r.first[i], 0.5f * (a[i] - b[i]), 1e-6); CHECK(r.first[i] == r.second[i]); } }
    { auto p = make({{Listen, Side}}); const auto r = run2(p, a, a); for (size_t i = 0; i < a.size(); i += 17) CHECK(r.first[i] == 0.0f); }   // a mono signal has no side
    { auto p = make({{Listen, Left}}); const auto r = run2(p, a, b); for (size_t i = 0; i < a.size(); i += 17) { CHECK(r.first[i] == a[i]); CHECK(r.second[i] == a[i]); } }
    { auto p = make({{Listen, Right}}); const auto r = run2(p, a, b); for (size_t i = 0; i < a.size(); i += 17) { CHECK(r.first[i] == b[i]); CHECK(r.second[i] == b[i]); } }
}
TEST_CASE("UT02 Phone speaker: no lows, a bump in the mids; Low cut; Level") {
    CHECK(gainAt({{Listen, Stereo}, {Phone, 1}}, 100) < -20.0); NEAR(gainAt({{Listen, Stereo}, {Phone, 1}}, 1200), 3.0 + 0.0, 1.2); NEAR(gainAt({{Listen, Stereo}, {Phone, 1}}, 5000), 0.0, 0.8);
    NEAR(gainAt({{Listen, Stereo}, {LowCut, 100}}, 50), -12.0, 2.5); NEAR(gainAt({{Listen, Stereo}, {LowCut, 100}}, 1000), 0.0, 0.1);
    NEAR(gainAt({{Listen, Stereo}, {LowCut, 20}}, 30), 0.0, 1e-4);   // the lowest position is Off
    NEAR(gainAt({{Listen, Stereo}, {Level, 6}}, 1000), 6.0, 0.01); NEAR(gainAt({{Listen, Stereo}, {Level, -24}}, 1000), -24.0, 0.01);
}
TEST_CASE("UT02 mono input and extreme values stay finite") {
    auto p = make({{Listen, Mono}, {Phone, 1}, {LowCut, 300}, {Level, 24}}); std::vector<float> l = noise(6, 1.0, 9); float* c[1] = {l.data()}; p.process(c, 1, static_cast<int>(l.size())); for (float v : l) CHECK(std::isfinite(v));
}
