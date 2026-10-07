#include "doctest.h"
#include "ut01/ut01.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::ut01;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::pair<std::vector<float>, std::vector<float>> go(Processor& p, std::vector<float> l, std::vector<float> r) { return run2(p, l, r); }
}

TEST_CASE("UT01 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"ut01.gain", "ut01.balance", "ut01.width", "ut01.phase.l", "ut01.phase.r", "ut01.swap", "ut01.mono", "ut01.channel"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Gain].min == -24); CHECK(s[Gain].max == 24); CHECK(s[Gain].def == 0); CHECK(s[Balance].def == 0); CHECK(std::string(s[Balance].minLabel) == "L"); CHECK(std::string(s[Balance].maxLabel) == "R");
    CHECK(s[Width].min == 0); CHECK(s[Width].max == 200); CHECK(s[Width].def == 100);
    for (int i : {PhaseL, PhaseR, Swap, Mono}) { CHECK(s[static_cast<size_t>(i)].labels == std::vector<std::string>{"Off", "On"}); CHECK(s[static_cast<size_t>(i)].def == 0); }
    CHECK(s[Channel].labels == std::vector<std::string>{"Both", "L only", "R only"}); CHECK(s[Channel].def == 0);
}
TEST_CASE("UT01 no delay; at the defaults the signal is bit-identical") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); const auto a = noise(-18, 1.0, 1), b = noise(-18, 1.0, 2); const auto r = go(p, a, b);
    for (size_t i = 0; i < a.size(); ++i) { CHECK(r.first[i] == a[i]); CHECK(r.second[i] == b[i]); }
}
TEST_CASE("UT01 Gain, polarity, swap, mono") {
    const auto a = sine(-18, 1.0, 440), b = sine(-24, 1.0, 880);
    { auto p = make({{Gain, 6}}); const auto r = go(p, a, b); NEAR(rmsDb(r.first, 24000, 48000) - rmsDb(a, 24000, 48000), 6.0, 0.02); NEAR(rmsDb(r.second, 24000, 48000) - rmsDb(b, 24000, 48000), 6.0, 0.02); }
    { auto p = make({{PhaseL, 1}}); const auto r = go(p, a, b); for (size_t i = 0; i < a.size(); i += 17) { NEAR(r.first[i], -a[i], 1e-7); CHECK(r.second[i] == b[i]); } }
    { auto p = make({{Swap, 1}}); const auto r = go(p, a, b); for (size_t i = 0; i < a.size(); i += 17) { CHECK(r.first[i] == b[i]); CHECK(r.second[i] == a[i]); } }
    { auto p = make({{Mono, 1}}); const auto r = go(p, a, b); for (size_t i = 0; i < a.size(); i += 17) { NEAR(r.first[i], 0.5f * (a[i] + b[i]), 1e-6); CHECK(r.first[i] == r.second[i]); } }
}
TEST_CASE("UT01 Width: 0 is mono, 200 doubles the side") {
    const auto a = noise(-18, 1.0, 1), b = noise(-18, 1.0, 2);
    { auto p = make({{Width, 0}}); const auto r = go(p, a, b); for (size_t i = 24000; i < a.size(); i += 17) { NEAR(r.first[i], 0.5f * (a[i] + b[i]), 1e-6); CHECK(r.first[i] == r.second[i]); } }
    { auto p = make({{Width, 200}}); const auto r = go(p, a, b); for (size_t i = 24000; i < a.size(); i += 17) { const double m = 0.5 * (a[i] + b[i]), s = 0.5 * (a[i] - b[i]); NEAR(r.first[i], m + 2 * s, 1e-6); NEAR(r.second[i], m - 2 * s, 1e-6); } }
}
TEST_CASE("UT01 Balance cuts the quieter side; Channel says who gets the Gain") {
    const auto a = sine(-18, 1.0, 440), b = a;
    { auto p = make({{Balance, 100}}); const auto r = go(p, a, b); CHECK(rmsDb(r.first, 24000, 48000) < -120.0); NEAR(rmsDb(r.second, 24000, 48000), rmsDb(b, 24000, 48000), 0.01); }
    { auto p = make({{Balance, -50}}); const auto r = go(p, a, b); NEAR(rmsDb(r.first, 24000, 48000), rmsDb(a, 24000, 48000), 0.01); NEAR(rmsDb(r.second, 24000, 48000) - rmsDb(b, 24000, 48000), 20 * std::log10(0.5), 0.01); }
    { auto p = make({{Gain, 6}, {Channel, LeftOnly}}); const auto r = go(p, a, b); NEAR(rmsDb(r.first, 24000, 48000) - rmsDb(a, 24000, 48000), 6.0, 0.02); for (size_t i = 0; i < a.size(); i += 17) CHECK(r.second[i] == b[i]); }
    { auto p = make({{Gain, -6}, {Channel, RightOnly}}); const auto r = go(p, a, b); for (size_t i = 0; i < a.size(); i += 17) CHECK(r.first[i] == a[i]); NEAR(rmsDb(r.second, 24000, 48000) - rmsDb(b, 24000, 48000), -6.0, 0.02); }
}
TEST_CASE("UT01 a change of Gain is smoothed (no jump)") {
    auto p = make(); std::vector<float> l(static_cast<size_t>(kFs), 0.5f), r = l; for (size_t off = 0; off < l.size(); off += 256) { if (off == 24064) p.setParam(Gain, 12); const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); }
    double worst = 0; for (size_t i = 1; i < l.size(); ++i) worst = std::max(worst, std::abs(double(l[i]) - l[i - 1])); CHECK(worst < 0.01);
    NEAR(l.back(), 0.5 * std::pow(10.0, 12.0 / 20.0), 0.01);
}
TEST_CASE("UT01 Gain per kind of track: classify, remember, suggest, save") {
    CHECK(classifyTrack("Lead Vocal") == Vocal); CHECK(classifyTrack("ボーカル 1") == Vocal); CHECK(classifyTrack("Kick In") == Drums); CHECK(classifyTrack("Bass DI") == Bass); CHECK(classifyTrack("Gtr L") == Guitar); CHECK(classifyTrack("Piano") == Keys); CHECK(classifyTrack("Drum Bus") == Drums); CHECK(classifyTrack("Mix Bus") == Bus); CHECK(classifyTrack("FX 3") == Other);
    auto p = make({{Gain, -4.5}}); p.setTrackName("Lead Vocal"); double db = 0; CHECK_FALSE(p.suggestedGainDb(db)); p.rememberGain(); CHECK(p.suggestedGainDb(db)); NEAR(db, -4.5, 1e-6);
    p.setTrackName("Bass DI"); CHECK_FALSE(p.suggestedGainDb(db));
    std::vector<uint8_t> blob; p.saveExtra(blob); auto q = make(); q.loadExtra(blob.data(), blob.size()); q.setTrackName("vox 2"); CHECK(q.suggestedGainDb(db)); NEAR(db, -4.5, 1e-6);
    q.loadExtra(blob.data(), 3);   // too short: nothing breaks
}
TEST_CASE("UT01 mono input and extreme values stay finite") {
    auto p = make({{Gain, 24}, {Width, 200}, {Balance, 100}}); std::vector<float> l = noise(6, 1.0, 9); float* c[1] = {l.data()}; p.process(c, 1, static_cast<int>(l.size())); for (float v : l) CHECK(std::isfinite(v));
}
