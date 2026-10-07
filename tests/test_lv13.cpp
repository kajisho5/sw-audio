#include "doctest.h"
#include "lv13/lv13.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv13;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
double gainAt(Processor& p, double f) { const auto y = run(p, sine(-30, 1.5, f)); return rmsDb(y, 24000, 72000) - (-30.0); }
const Set kOff = {{Hpf, 20}, {Lpf, 20000}};
Set with(Set a, Set b) { a.insert(a.end(), b.begin(), b.end()); return a; }
}

TEST_CASE("LV13 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams)); CHECK(kNumParams == 26);
    const double f[] = {100, 250, 630, 1600, 4000, 10000};
    for (int b = 0; b < kBands; ++b) {
        CHECK(s[static_cast<size_t>(id(b, Type))].labels == std::vector<std::string>{"Bell", "Shelf"}); CHECK(s[static_cast<size_t>(id(b, Type))].def == 0);
        CHECK(s[static_cast<size_t>(id(b, Freq))].min == 20); CHECK(s[static_cast<size_t>(id(b, Freq))].max == 20000); CHECK(s[static_cast<size_t>(id(b, Freq))].def == f[b]);
        CHECK(s[static_cast<size_t>(id(b, Gain))].min == -15); CHECK(s[static_cast<size_t>(id(b, Gain))].max == 15); CHECK(s[static_cast<size_t>(id(b, Gain))].def == 0);
        CHECK(s[static_cast<size_t>(id(b, Q))].min == 0.3); CHECK(s[static_cast<size_t>(id(b, Q))].max == 10); CHECK(s[static_cast<size_t>(id(b, Q))].def == 2.0); CHECK(s[static_cast<size_t>(id(b, Q))].curve == Curve::Log);
    }
    CHECK(std::string(s[0].name) == "Band 1 Type"); CHECK(std::string(s[static_cast<size_t>(id(5, Q))].name) == "Band 6 Q");
    CHECK(s[Hpf].min == 20); CHECK(s[Hpf].max == 400); CHECK(s[Hpf].def == 90); CHECK(std::string(s[Hpf].minLabel) == "Off");
    CHECK(s[Lpf].min == 5000); CHECK(s[Lpf].max == 20000); CHECK(s[Lpf].def == 18000); CHECK(std::string(s[Lpf].maxLabel) == "Off");
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV13 flat with HPF and LPF Off passes the signal untouched") {
    auto p = make(kOff); const auto x = noise(-20, 1.0, 3), y = run(p, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]);
}
TEST_CASE("LV13 a bell: gain at its frequency, width by Q") {
    auto p = make(with(kOff, {{id(3, Gain), 9.0}, {id(3, Freq), 2000}})); CHECK(std::abs(gainAt(p, 2000) - 9.0) < 0.3);
    auto wide = make(with(kOff, {{id(3, Gain), 9.0}, {id(3, Freq), 2000}, {id(3, Q), 0.5}})), narrow = make(with(kOff, {{id(3, Gain), 9.0}, {id(3, Freq), 2000}, {id(3, Q), 8.0}}));
    CHECK(gainAt(wide, 3000) > gainAt(narrow, 3000) + 3.0);
    auto n = make(with(kOff, {{id(1, Gain), -12.0}, {id(1, Freq), 300}})); CHECK(std::abs(gainAt(n, 300) - (-12.0)) < 0.3);
}
TEST_CASE("LV13 a shelf: low under 1 kHz, high from 1 kHz") {
    auto lo = make(with(kOff, {{id(0, Type), 1}, {id(0, Freq), 200}, {id(0, Gain), 6.0}, {id(0, Q), 0.7}})); CHECK(gainAt(lo, 40) > 5.0); auto lo2 = make(with(kOff, {{id(0, Type), 1}, {id(0, Freq), 200}, {id(0, Gain), 6.0}, {id(0, Q), 0.7}})); CHECK(std::abs(gainAt(lo2, 5000)) < 0.5);
    auto hi = make(with(kOff, {{id(5, Type), 1}, {id(5, Freq), 6000}, {id(5, Gain), -6.0}, {id(5, Q), 0.7}})); CHECK(gainAt(hi, 16000) < -5.0); auto hi2 = make(with(kOff, {{id(5, Type), 1}, {id(5, Freq), 6000}, {id(5, Gain), -6.0}, {id(5, Q), 0.7}})); CHECK(std::abs(gainAt(hi2, 200)) < 0.5);
}
TEST_CASE("LV13 HPF and LPF") {
    auto p = make({{Hpf, 400}, {Lpf, 5000}}); CHECK(gainAt(p, 60) < -15.0); auto q = make({{Hpf, 400}, {Lpf, 5000}}); CHECK(gainAt(q, 18000) < -15.0);
    auto d = make(); CHECK(gainAt(d, 40) < -6.0);   // the default HPF at 90 Hz
}
TEST_CASE("LV13 the analyser suggests cutting a peak, and nothing for flat noise") {
    auto pink = noise(-30, 8.0, 5); { double b0 = 0, b1 = 0, b2 = 0; for (auto& v : pink) { const double w = v; b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913; v = static_cast<float>(0.2 * (b0 + b1 + b2 + w * 0.1848)); } }
    auto p = make(kOff); run(p, pink); CHECK(p.suggestions().empty());
    auto withPeak = pink; const auto t = sine(-18, 8.0, 3150); for (size_t i = 0; i < t.size(); ++i) withPeak[i] += t[i];
    auto q = make(kOff); run(q, withPeak); const auto s = q.suggestions(); REQUIRE_FALSE(s.empty()); CHECK(std::abs(std::log2(s[0].freqHz / 3150.0)) < 0.2); CHECK(s[0].gainDb < -2.0); CHECK(s[0].gainDb >= -6.0); CHECK(s.size() <= 3);
}
TEST_CASE("LV13 mono, odd blocks, silence, before prepare") {
    auto p = make({{id(2, Gain), 6.0}}); std::vector<float> l = noise(-20, 1.5, 3); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; p.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    auto q = make(); for (float v : run(q, std::vector<float>(48000, 0.0f))) REQUIRE(v == 0.0f);
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f);
}
