#include "doctest.h"
#include "sa04/sa04.hpp"
#include "tu.hpp"
#include "os_helpers.hpp"
using namespace sw;
using namespace sw::sa04;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
double gain(Set s, double f, double db = -50) { auto p = make(s); return rmsDb(run(p, sine(db, 2, f))) - db; }
double h3(Set s, double f, double db) { auto p = make(s); return harmDb(run(p, sine(db, 2, f)), f, 3); }
}

TEST_CASE("SA04 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"sa04.iron", "sa04.gain", "sa04.load", "sa04.lowweight", "sa04.topair", "sa04.output", "sa04.pad", "sa04.os", "sa04.unit"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Iron].labels == std::vector<std::string>{"Nickel", "Steel", "Mu"}); CHECK(s[Iron].def == 1);
    CHECK(s[Gain].min == 0); CHECK(s[Gain].max == 60); CHECK(s[Gain].def == 30);
    CHECK(s[Load].def == 0.5); CHECK(std::string(s[Load].minLabel) == "Low"); CHECK(std::string(s[Load].maxLabel) == "High");
    CHECK(s[LowWeight].max == 10); CHECK(s[LowWeight].def == 0); CHECK(s[TopAir].max == 10); CHECK(s[TopAir].def == 0);
    CHECK(s[Output].min == -10); CHECK(s[Output].max == 10); CHECK(s[Pad].labels == std::vector<std::string>{"Off", "On"}); CHECK(s[Pad].def == 0);
}
TEST_CASE("SA04 Gain scale: 30 = 0 dB, -30..+30 dB over 0..60; Pad -20 dB") {
    NEAR(gain({}, 1000), 0.0, 0.5); NEAR(gain({{Gain, 40}}, 1000), 10.0, 0.5); NEAR(gain({{Gain, 15}}, 1000), -15.0, 0.5); NEAR(gain({{Gain, 0}}, 1000), -30.0, 0.5);
    NEAR(gain({{Pad, 1}}, 1000), -20.0, 0.5);
}
TEST_CASE("SA04 flat at the defaults between 100 Hz and 10 kHz") {
    for (double f : {100.0, 300.0, 1000.0, 3000.0, 10000.0}) NEAR(gain({}, f), 0.0, 0.8);
}
TEST_CASE("SA04 Iron: Mu saturates earliest, Nickel latest") {
    const double a = h3({{Iron, 0}, {Gain, 42}}, 1000, -30), b = h3({{Iron, 1}, {Gain, 42}}, 1000, -30), c = h3({{Iron, 2}, {Gain, 42}}, 1000, -30);
    CHECK(c > b + 2.0); CHECK(b > a + 2.0);
}
TEST_CASE("SA04 saturation is stronger in the lows") {
    CHECK(h3({{Gain, 42}}, 50, -30) > h3({{Gain, 42}}, 1000, -30) + 6.0);
}
TEST_CASE("SA04 Load: a high source impedance lifts the high-frequency resonance and thins the lows") {
    CHECK(gain({{Load, 1}}, 16000) > gain({{Load, 0}}, 16000) + 2.0);
    CHECK(gain({{Load, 1}}, 30) < gain({{Load, 0}}, 30) - 1.0);
    NEAR(gain({{Load, 1}}, 1000) - gain({{Load, 0}}, 1000), 0.0, 0.7);
}
TEST_CASE("SA04 Low weight and Top air shape the ends") {
    CHECK(gain({{LowWeight, 10}}, 80) > gain({}, 80) + 4.0); NEAR(gain({{LowWeight, 10}}, 2000), gain({}, 2000), 0.7);
    CHECK(gain({{TopAir, 10}}, 12000) > gain({}, 12000) + 4.0); NEAR(gain({{TopAir, 10}}, 1000), gain({}, 1000), 0.7);
}
TEST_CASE("SA04 silence stays silent, extreme input finite, latency 0") {
    auto p = make({{Gain, 60}, {LowWeight, 10}, {TopAir, 10}, {Load, 1}, {Iron, 2}});
    for (float v : run(p, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f);
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : run(p, x)) REQUIRE(std::isfinite(v));
    CHECK(p.latencySamples() == 0);
}

// the common oversampling setting (spec 共通機能: 1x / 2x / 4x, default 2x)
TEST_CASE("SA04: the oversampling parameter is the last one, 1x / 2x / 4x, default 2x, and the shaper follows it") {
    const auto& s = specs();
    CHECK(s[Oversample].steps == std::vector<double>{1, 2, 4}); CHECK(s[Oversample].def == 2.0); CHECK(Oversample == kNumParams - 2);
    auto alias = [](int os) { auto p = make({{Gain, 60}, {Oversample, static_cast<double>(os)}}); return ost::relDb(p, 15000, 3000, 0.1); };
    const double a1 = alias(1), a2 = alias(2), a4 = alias(4);
    INFO("15 kHz, alias at 3 kHz: 1x " << a1 << " dB, 2x " << a2 << " dB, 4x " << a4 << " dB");
    CHECK(a1 > -50.0); CHECK(a2 < a1 - 15.0); CHECK(ost::notWorse(a4, a2));
}
