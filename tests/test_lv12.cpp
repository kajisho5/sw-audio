#include "doctest.h"
#include "lv12/lv12.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv12;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
double gainAt(Processor& p, double f) { const auto y = run(p, sine(-30, 1.5, f)); return rmsDb(y, 24000, 72000) - (-30.0); }
}

TEST_CASE("LV12 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams)); CHECK(kNumParams == 31 + 7 + 31);
    CHECK(bandCenterHz(0) == 20); CHECK(bandCenterHz(17) == 1000); CHECK(bandCenterHz(30) == 20000);
    for (int b = 0; b < kBands; ++b) { CHECK(s[static_cast<size_t>(b)].min == -12); CHECK(s[static_cast<size_t>(b)].max == 12); CHECK(s[static_cast<size_t>(b)].def == 0); CHECK(s[static_cast<size_t>(BandR0 + b)].def == 0); }
    CHECK(std::string(s[0].name) == "Band 20 Hz"); CHECK(std::string(s[17].name) == "Band 1 kHz"); CHECK(std::string(s[30].name) == "Band 20 kHz");
    CHECK(s[Edit].labels == std::vector<std::string>{"Left", "Right", "Both"}); CHECK(s[Edit].def == 2);
    CHECK(s[LinkLR].def == 1); CHECK(s[Hpf].min == 20); CHECK(s[Hpf].max == 200); CHECK(s[Hpf].def == 40); CHECK(std::string(s[Hpf].minLabel) == "Off");
    CHECK(s[Lpf].min == 5000); CHECK(s[Lpf].max == 20000); CHECK(s[Lpf].def == 18000); CHECK(std::string(s[Lpf].maxLabel) == "Off");
    CHECK(s[Output].min == -12); CHECK(s[Output].max == 12); CHECK(s[Output].def == 0); CHECK(s[RtaOverlay].def == 1); CHECK(s[FeedbackGuardOn].def == 1);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV12 flat with HPF and LPF Off passes the signal untouched") {
    auto p = make({{Hpf, 20}, {Lpf, 20000}}); const auto x = noise(-20, 1.0, 3), y = run(p, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]);
}
TEST_CASE("LV12 a fader moves its band by its gain; the neighbours stay near") {
    for (int b : {5, 17, 25}) {
        auto p = make({{Hpf, 20}, {Lpf, 20000}, {b, 9.0}}); const double f = bandCenterHz(b);
        CHECK(std::abs(gainAt(p, f) - 9.0) < 0.6);
        auto q = make({{Hpf, 20}, {Lpf, 20000}, {b, -9.0}}); CHECK(std::abs(gainAt(q, f) - (-9.0)) < 0.6);
        auto r = make({{Hpf, 20}, {Lpf, 20000}, {b, 9.0}}); CHECK(std::abs(gainAt(r, f * 2.0)) < 1.0);   // an octave away: the constant-Q bell has almost nothing left
    }
}
TEST_CASE("LV12 Link L/R: right follows left; with Link Off it takes its own gains") {
    auto p = make({{Hpf, 20}, {Lpf, 20000}, {17, 8.0}}); const auto y = run2(p, sine(-30, 1.5, 1000), sine(-30, 1.5, 1000)); CHECK(std::abs(rmsDb(y.second, 24000, 72000) - rmsDb(y.first, 24000, 72000)) < 0.01);
    auto q = make({{Hpf, 20}, {Lpf, 20000}, {LinkLR, 0}, {17, 8.0}, {BandR0 + 17, -6.0}}); const auto z = run2(q, sine(-30, 1.5, 1000), sine(-30, 1.5, 1000));
    CHECK(std::abs((rmsDb(z.first, 24000, 72000) + 30.0) - 8.0) < 0.6); CHECK(std::abs((rmsDb(z.second, 24000, 72000) + 30.0) - (-6.0)) < 0.6);
}
TEST_CASE("LV12 HPF and LPF") {
    auto p = make({{Hpf, 200}, {Lpf, 5000}}); CHECK(gainAt(p, 40) < -15.0); auto q = make({{Hpf, 200}, {Lpf, 5000}}); CHECK(gainAt(q, 18000) < -15.0); auto r = make({{Hpf, 200}, {Lpf, 5000}}); CHECK(std::abs(gainAt(r, 1000)) < 0.5);
}
TEST_CASE("LV12 Flat hands 62 zeros to the host; a value set afterwards cancels its write") {
    Processor p; p.prepare(kFs, 256); p.setParam(3, 7.0); p.setParam(BandR0 + 3, -4.0); p.flat(); int n = 0, id; double v; while (p.takeParamWrite(id, v)) { ++n; CHECK(v == 0.0); CHECK(((id >= 0 && id < kBands) || (id >= BandR0 && id < BandR0 + kBands))); } CHECK(n == 62);
    Processor q; q.prepare(kFs, 256); q.flat(); q.setParam(5, 3.0); n = 0; while (q.takeParamWrite(id, v)) { ++n; CHECK(id != 5); } CHECK(n == 61);
    auto r = make({{Hpf, 20}, {Lpf, 20000}, {17, 9.0}}); r.flat(); CHECK(std::abs(gainAt(r, 1000)) < 0.3);
}
TEST_CASE("LV12 Feedback guard marks the ringing band and cuts nothing") {
    auto howl = sine(-30, 4.0, 2500); auto nz = noise(-70, 4.0, 2); for (size_t i = 0; i < howl.size(); ++i) howl[i] += nz[i];
    auto p = make({{Hpf, 20}, {Lpf, 20000}}); const auto y = run(p, howl); const unsigned m = p.flaggedBands();
    CHECK(m == (1u << 21)); for (size_t i = 100000; i < 100100; ++i) NEAR(y[i], howl[i], 1e-6);
    auto q = make({{Hpf, 20}, {Lpf, 20000}, {FeedbackGuardOn, 0}}); run(q, howl); CHECK(q.flaggedBands() == 0u);
}
TEST_CASE("LV12 RTA levels follow the input") {
    auto p = make({{Hpf, 20}, {Lpf, 20000}}); run(p, sine(-20, 3.0, 1000)); CHECK(p.rtaDb(17) > -30.0); CHECK(p.rtaDb(8) < p.rtaDb(17) - 30.0);
    auto q = make({{RtaOverlay, 0}}); run(q, sine(-20, 2.0, 1000)); CHECK(q.rtaDb(17) <= -199.0);
}
TEST_CASE("LV12 mono, odd blocks, silence, before prepare") {
    auto p = make({{17, 6.0}}); std::vector<float> l = noise(-20, 1.5, 3); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; p.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    auto q = make(); for (float v : run(q, std::vector<float>(48000, 0.0f))) REQUIRE(v == 0.0f);
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f);
}
