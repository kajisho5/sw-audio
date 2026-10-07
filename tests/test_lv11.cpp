#include "doctest.h"
#include "lv11/lv11.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv11;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
}

TEST_CASE("LV11 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Mic].labels == std::vector<std::string>{"Live", "Push to talk", "Off"}); CHECK(s[Mic].def == 0);
    CHECK(s[AutoMute].labels == std::vector<std::string>{"Off", "On silence"}); CHECK(s[AutoMute].def == 1);
    CHECK(s[Silence].min == -70); CHECK(s[Silence].max == -30); CHECK(s[Silence].def == -48);
    CHECK(s[Hold].min == 0.5); CHECK(s[Hold].max == 10); CHECK(s[Hold].def == 3.0);
    CHECK(s[Fade].min == 5); CHECK(s[Fade].max == 200); CHECK(s[Fade].def == 20); CHECK(s[Fade].curve == Curve::Log);
    CHECK(s[DuckOthers].min == -30); CHECK(s[DuckOthers].max == 0); CHECK(s[DuckOthers].def == -10);
    CHECK(s[HoldToCough].automatable == false);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV11 Live passes the voice bit for bit; Off is silent") {
    const auto x = sine(-20, 1.0, 500); auto p = make(); const auto y = run(p, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]);
    auto q = make({{Mic, Off}}); for (float v : run(q, x)) REQUIRE(v == 0.0f);
}
TEST_CASE("LV11 cough button closes and opens in Fade ms") {
    auto p = make({{Fade, 20}}); const auto x = sine(-20, 2.0, 500); std::vector<float> a(x.begin(), x.begin() + 24000); run(p, a);
    p.setParam(HoldToCough, 1); const auto y = run(p, std::vector<float>(x.begin(), x.begin() + 4800));
    CHECK(std::abs(y[100]) > 0.0f); for (size_t i = 1200; i < 4800; ++i) REQUIRE(y[i] == 0.0f);   // 20 ms = 960 samples: closed after that
    CHECK(std::abs(p.gain()) < 1e-9);
    p.setParam(HoldToCough, 0); run(p, std::vector<float>(x.begin(), x.begin() + 2400)); CHECK(p.gain() == 1.0);
}
TEST_CASE("LV11 Push to talk: closed until the button is held") {
    auto p = make({{Mic, PushToTalk}}); const auto x = sine(-20, 1.0, 500); for (float v : run(p, x)) REQUIRE(v == 0.0f);
    p.setParam(HoldToCough, 1); const auto y = run(p, x); CHECK(rmsDb(y, 24000, 48000) > -21.0);
}
TEST_CASE("LV11 Auto mute: silence for Hold closes the mic, a voice opens it") {
    auto p = make({{Hold, 1}, {Silence, -48}, {Fade, 10}});
    run(p, sine(-20, 1.0, 500)); CHECK(p.isOpen()); CHECK_FALSE(p.autoMuted());
    run(p, noise(-60, 0.5, 3)); CHECK(p.isOpen());                               // quiet for only 0.5 s
    run(p, noise(-60, 1.5, 3)); CHECK(p.autoMuted()); CHECK_FALSE(p.isOpen());
    const auto y = run(p, sine(-30, 1.0, 500)); CHECK(p.isOpen()); CHECK(rmsDb(y, 24000, 48000) > -31.0);
    auto q = make({{AutoMute, 0}, {Hold, 0.5}}); run(q, noise(-60, 3.0, 3)); CHECK(q.isOpen());
}
TEST_CASE("LV11 Duck others: a talking mic lowers the other instances") {
    Processor a = make({{DuckOthers, -12}}); Processor b = make({{DuckOthers, -6}});
    CHECK(a.linkIndex() >= 0); CHECK(b.linkIndex() >= 0); CHECK(a.linkIndex() != b.linkIndex());
    // a talks (500 Hz at -20), b hears a quiet voice at -20 too: b must go down by a's -12 dB; a is not ducked by b while b is silent
    const auto x = sine(-20, 3.0, 500), z = std::vector<float>(x.size(), 0.0f);
    std::vector<float> ya = x, yb = x, ra = x, rb = x;
    for (size_t off = 0; off < x.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, x.size() - off)); float* ca[2] = {ya.data() + off, ra.data() + off}; float* cb[2] = {yb.data() + off, rb.data() + off}; a.process(ca, 2, n); b.process(cb, 2, n); }
    CHECK(b.duckDb() < -11.0); CHECK(a.duckDb() > -7.0);   // both talk: each gets the other's amount; a sees b's -6
    CHECK(std::abs(a.duckDb() - (-6.0)) < 0.5); CHECK(std::abs(b.duckDb() - (-12.0)) < 0.5);
    (void)z;
}
TEST_CASE("LV11 a mic that leaves frees its slot; mono; odd blocks; before prepare") {
    int idx; { Processor a = make(); idx = a.linkIndex(); Processor b = make(); CHECK(b.linkIndex() != idx); }
    Processor c = make(); CHECK(c.linkIndex() == 0);
    auto p = make(); std::vector<float> l = sine(-20, 1.0, 500); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* ch[1] = {l.data() + off}; p.process(ch, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    Processor z; std::vector<float> a(256, 0.3f); float* ch[1] = {a.data()}; z.process(ch, 1, 256); CHECK(a[0] == 0.3f);
}
