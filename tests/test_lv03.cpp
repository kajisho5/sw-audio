#include "doctest.h"
#include "lv03/lv03.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv03;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
// every stage out of the way
Set neutral() { return {{Trim, 0}, {Hpf, 20}, {GateThresh, -80}, {GateRange, 0}, {EqLow, 0}, {EqMid, 0}, {EqHigh, 0}, {FbGuard, 0}, {CompThresh, 0}, {CompRatio, 1}, {DeessAmount, 0}}; }
Processor make(Set base, Set set = {}) { Processor p; for (auto& s : base) p.setParam(s.first, s.second); for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
}

TEST_CASE("LV03 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Mic].labels == std::vector<std::string>{"Handheld", "Lavalier", "Headset", "Podium"});
    CHECK(s[Trim].min == -20); CHECK(s[Trim].max == 40); CHECK(s[Trim].def == 6);
    CHECK(s[Hpf].min == 20); CHECK(s[Hpf].max == 400); CHECK(s[Hpf].def == 80); CHECK(std::string(s[Hpf].minLabel) == "Off");
    CHECK(s[GateThresh].min == -80); CHECK(s[GateThresh].def == -42); CHECK(s[GateRange].min == -80); CHECK(s[GateRange].max == 0); CHECK(s[GateRange].def == -30);
    CHECK(s[EqLow].def == -2); CHECK(s[EqMid].def == 2); CHECK(s[EqHigh].def == 1.5); CHECK(s[EqMidF].min == 200); CHECK(s[EqMidF].max == 8000); CHECK(s[EqMidF].def == 1200);
    CHECK(s[FbGuard].def == 1); CHECK(s[CompThresh].min == -40); CHECK(s[CompThresh].def == -18); CHECK(s[CompRatio].min == 1); CHECK(s[CompRatio].max == 10); CHECK(s[CompRatio].def == 3);
    CHECK(s[DeessAmount].max == 12); CHECK(s[DeessAmount].def == 4); CHECK(s[DeessFreq].min == 3000); CHECK(s[DeessFreq].max == 12000); CHECK(s[DeessFreq].def == 6500);
    CHECK(s[Out].min == -20); CHECK(s[Out].max == 10); CHECK(s[Out].def == -2);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV03 Trim and Ø") {
    const auto x = sine(-30, 1.0, 1000);
    { auto p = make(neutral(), {{Trim, 6}}); const auto y = run(p, x); CHECK(std::abs(rmsDb(y) - (rmsDb(x) + 6.0206)) < 0.05); }
    { auto p = make(neutral(), {{Phase, 1}}); const auto y = run(p, x); for (size_t i = 100; i < x.size(); i += 53) NEAR(y[i], -x[i], 1e-6); }
    { auto p = make(neutral()); const auto y = run(p, x); for (size_t i = 100; i < x.size(); i += 53) NEAR(y[i], x[i], 1e-5); }
}
TEST_CASE("LV03 HPF") {
    auto p = make(neutral(), {{Hpf, 200}}); const auto lo = run(p, sine(-20, 1.0, 50)), hi = run(p, sine(-20, 1.0, 1000));
    CHECK(rmsDb(lo) < -20 - 20.0); CHECK(std::abs(rmsDb(hi) - (-20.0)) < 0.5);
}
TEST_CASE("LV03 gate cuts the room and opens for the voice") {
    auto p = make(neutral(), {{GateThresh, -40}, {GateRange, -30}});
    const auto room = run(p, noise(-60, 2.0, 3)); CHECK(rmsDb(room, 48000, 96000) < -60 - 25.0);
    const auto v = run(p, sine(-20, 1.0, 800)); CHECK(std::abs(rmsDb(v, 24000, 48000) - (-20.0)) < 0.5);
}
TEST_CASE("LV03 EQ bands") {
    auto p = make(neutral(), {{EqLow, 12}}); CHECK(rmsDb(run(p, sine(-30, 1.0, 40))) - (-30.0) > 10.5);
    auto q = make(neutral(), {{EqMid, 12}, {EqMidF, 2000}}); CHECK(std::abs(rmsDb(run(q, sine(-30, 1.0, 2000))) - (-30.0 + 12.0)) < 0.5); CHECK(std::abs(rmsDb(run(q, sine(-30, 1.0, 100))) - (-30.0)) < 0.7);
    auto r = make(neutral(), {{EqHigh, -12}}); CHECK(rmsDb(run(r, sine(-30, 1.0, 12000))) - (-30.0) < -10.5);
}
TEST_CASE("LV03 compressor follows threshold and ratio") {
    auto p = make(neutral(), {{CompThresh, -18}, {CompRatio, 4}}); const auto y = run(p, sine(-6, 3.0, 700));   // 12 dB over: 9 dB of reduction
    CHECK(std::abs(rmsDb(y) - (-6.0 - 9.0)) < 1.5); CHECK(p.compGainDb() < -7.0);
    auto q = make(neutral(), {{CompThresh, -18}, {CompRatio, 4}}); CHECK(std::abs(rmsDb(run(q, sine(-30, 2.0, 700))) - (-30.0)) < 0.3);   // under the threshold
}
TEST_CASE("LV03 de-esser cuts sibilance only, up to Amount") {
    auto p = make(neutral(), {{DeessAmount, 6}, {DeessFreq, 6000}}); const auto s = run(p, sine(-20, 1.0, 9000)); CHECK(std::abs((rmsDb(s, 24000, 48000) - (-20.0)) - (-6.0)) < 1.2);
    auto q = make(neutral(), {{DeessAmount, 6}, {DeessFreq, 6000}}); CHECK(std::abs(rmsDb(run(q, sine(-20, 1.0, 1000))) - (-20.0)) < 0.2);
    auto r = make(neutral(), {{DeessAmount, 12}, {DeessFreq, 6000}}); CHECK(rmsDb(run(r, sine(-20, 1.0, 9000)), 24000, 48000) < -20.0 - 8.0);
}
TEST_CASE("LV03 feedback guard: on cuts a howl, off does not") {
    auto howl = sine(-30, 4.0, 2800); auto nz = noise(-70, 4.0, 2); for (size_t i = 0; i < howl.size(); ++i) howl[i] += nz[i];
    auto p = make(neutral(), {{FbGuard, 1}}); const auto y = run(p, howl); CHECK(p.guard().activeCount() == 1); CHECK(rmsDb(y, 3 * 48000, 4 * 48000) < rmsDb(howl, 3 * 48000, 4 * 48000) - 5.0);
    auto q = make(neutral(), {{FbGuard, 0}}); run(q, howl); CHECK(q.guard().activeCount() == 0);
}
TEST_CASE("LV03 Mic writes the strip; a value set afterwards cancels its write") {
    for (int m = 0; m < 4; ++m) {
        const auto pre = micPreset(m); CHECK(pre.size() == 14);
        for (const auto& w : pre) { REQUIRE(w.first != Mic); REQUIRE(w.first != Out); const auto& sp = specs()[static_cast<size_t>(w.first)]; CHECK(w.second >= sp.min); CHECK(w.second <= sp.max); }
    }
    { const auto h = micPreset(Handheld); for (const auto& w : h) if (w.first != Phase) CHECK(w.second == specs()[static_cast<size_t>(w.first)].def); }
    Processor p; p.prepare(kFs, 256); p.setParam(Mic, Podium); int n = 0, id; double v; bool sawTrim = false;
    while (p.takeParamWrite(id, v)) { ++n; if (id == Trim) { sawTrim = true; CHECK(v == 18); } REQUIRE(id != Out); }
    CHECK(n == 14); CHECK(sawTrim);
    Processor q; q.prepare(kFs, 256); q.setParam(Mic, Lavalier); q.setParam(Trim, 3.0); n = 0; while (q.takeParamWrite(id, v)) { ++n; CHECK(id != Trim); } CHECK(n == 13);
    // Podium's HPF is applied at once (the strip does not wait for the host)
    Set n2 = neutral(); n2.erase(n2.begin() + 1);   // keep Podium's HPF (120 Hz), everything else out of the way
    auto r = make({{Mic, Podium}}, n2); const auto lo = run(r, sine(-20, 2.0, 60)); CHECK(rmsDb(lo) < -20 - 8.0);
}
TEST_CASE("LV03 finite, mono, odd blocks, before prepare, silence") {
    auto p = make({}); auto l = noise(-10, 2.0, 5); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; p.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    auto q = make({}); for (float v : run(q, std::vector<float>(48000, 0.0f))) REQUIRE(v == 0.0f);
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f);
}
