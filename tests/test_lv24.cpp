#include "doctest.h"
#include "lv24/lv24.hpp"
#include "tu.hpp"
#include "voice.hpp"
using namespace sw;
using namespace sw::lv24;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> impulse(size_t n, float a = 0.5f) { std::vector<float> x(n, 0.0f); x[0] = a; return x; }
// decay time: the time the tail's energy envelope needs to fall by 60 dB (extrapolated from the -5 .. -25 dB part, T20 x 3)
double rt60(const std::vector<float>& y) {
    std::vector<double> e(y.size()); double acc = 0; for (size_t i = y.size(); i-- > 0;) { acc += double(y[i]) * y[i]; e[i] = acc; }
    auto when = [&](double db) { const double t = e[0] * std::pow(10.0, db / 10.0); for (size_t i = 0; i < e.size(); ++i) if (e[i] < t) return static_cast<double>(i) / kFs; return static_cast<double>(e.size()) / kFs; };
    return 3.0 * (when(-25.0) - when(-5.0));
}
}

TEST_CASE("LV24 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Type].labels == std::vector<std::string>{"Vocal hall", "Room", "Plate"}); CHECK(s[Type].def == 0);
    CHECK(s[Decay].min == 0.3); CHECK(s[Decay].max == 5); CHECK(s[Decay].def == 1.8); CHECK(s[Decay].curve == Curve::Log);
    CHECK(s[PreDelay].min == 0); CHECK(s[PreDelay].max == 200); CHECK(s[PreDelay].def == 30); CHECK(s[PreDelay].curve == Curve::Skew); CHECK(s[PreDelay].skew == 2);
    CHECK(s[Tone].labels == std::vector<std::string>{"Warm", "Neutral", "Bright"}); CHECK(s[Tone].def == 0); CHECK(s[Mix].def == 18); CHECK(s[Duck].def == 0);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV24 silence in, silence out; the tail is finite and decays") {
    auto p = make(); for (float v : run(p, std::vector<float>(48000, 0.0f))) REQUIRE(v == 0.0f);
    auto q = make(); const auto y = run(q, impulse(48000 * 8)); for (float v : y) REQUIRE(std::isfinite(v)); CHECK(rmsDb(y, 48000 * 7, 48000 * 8) < rmsDb(y, 3000, 12000) - 40.0);
}
TEST_CASE("LV24 Pre-delay: nothing before it") {
    auto p = make({{PreDelay, 50}}); const auto y = run(p, impulse(48000)); double early = 0; for (size_t i = 0; i < 2300; ++i) early = std::max(early, static_cast<double>(std::abs(y[i]))); CHECK(early < 1e-6);
    double later = 0; for (size_t i = 2400 + 1400; i < 2400 + 4800; ++i) later = std::max(later, static_cast<double>(std::abs(y[i]))); CHECK(later > 1e-3);
}
TEST_CASE("LV24 Decay sets the reverberation time") {
    for (double d : {0.6, 1.8, 3.5}) { auto p = make({{Decay, d}, {PreDelay, 0}}); const auto y = run(p, impulse(static_cast<size_t>(48000 * (d * 2.5 + 1)))); const double t = rt60(y); CHECK(t > d * 0.7); CHECK(t < d * 1.4); }
}
TEST_CASE("LV24 Type: Room is denser and shorter-looped than Vocal hall; Tone: Bright has more highs") {
    auto hall = make({{Type, VocalHall}}), room = make({{Type, Room}});
    const auto a = run(hall, impulse(48000 * 3)), b = run(room, impulse(48000 * 3)); CHECK(rmsDb(b, 0, 4800) != rmsDb(a, 0, 4800));
    auto warm = make({{Tone, 0}}), bright = make({{Tone, 2}}); const auto w = run(warm, impulse(48000 * 3)), br = run(bright, impulse(48000 * 3));
    auto hf = [&](const std::vector<float>& y) { return binDb(y, 9000, 4800, 96000) - binDb(y, 300, 4800, 96000); }; CHECK(hf(br) > hf(w) + 3.0);
}
TEST_CASE("LV24 the wet level is about the same for any Decay and Type (unit-energy scaling)") {
    double lv[4]; int k = 0; for (auto s : {Set{{Decay, 0.5}}, Set{{Decay, 3.0}}, Set{{Type, Room}}, Set{{Type, Plate}, {Decay, 1.0}}}) { auto p = make(s); const auto y = run(p, noise(-20, 6.0, 3)); lv[k++] = rmsDb(y, 96000, 288000); }
    for (int i = 1; i < 4; ++i) CHECK(std::abs(lv[i] - lv[0]) < 4.0);
}
TEST_CASE("LV24 Duck lowers the reverb while someone speaks, not in the pauses") {
    const auto v = voice(180.0, 3.0, 0.0, 700.0, 1800.0, 0.1);
    auto on = make({{Duck, 1}}); run(on, v); CHECK(on.duckGainDb() < -6.0);
    auto off = make({{Duck, 0}}); run(off, v); CHECK(off.duckGainDb() == doctest::Approx(0.0));
    run(on, std::vector<float>(48000 * 3, 0.0f)); CHECK(on.duckGainDb() > -1.0);
}
TEST_CASE("LV24 mono, odd blocks, before prepare") {
    auto p = make(); std::vector<float> l = noise(-20, 1.0, 3); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; p.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f);
}
