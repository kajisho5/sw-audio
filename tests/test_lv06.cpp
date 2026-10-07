#include "doctest.h"
#include "lv06/lv06.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv06;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
}

TEST_CASE("LV06 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Target].labels == std::vector<std::string>{"Stream -14", "Podcast -16", "Broadcast -24", "Custom"}); CHECK(s[Target].def == 0);
    CHECK(s[Custom].min == -30); CHECK(s[Custom].max == -5);
    CHECK(s[Ride].labels == std::vector<std::string>{"Slow", "Medium", "Fast"}); CHECK(s[Ride].def == 0);
    CHECK(s[MaxBoost].min == 0); CHECK(s[MaxBoost].max == 12); CHECK(s[MaxBoost].def == 6);
    CHECK(s[Ceiling].min == -3); CHECK(s[Ceiling].max == 0); CHECK(s[Ceiling].def == -1);
    CHECK(s[MonoSafe].def == 0);
    CHECK(targetLufs(0, -9) == -14); CHECK(targetLufs(1, -9) == -16); CHECK(targetLufs(2, -9) == -24); CHECK(targetLufs(3, -9) == -9);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV06 rides to the target: quiet input is raised, loud input is lowered, at the Ride speed") {
    // white noise at -26 dBFS rms is about -19.9 LUFS (K-weighting adds 6.1 dB)
    auto p = make({{Ride, 2}, {MaxBoost, 12}}); const auto x = noise(-30, 40.0, 3); const auto y = run(p, x);
    const double lufs = p.loudnessLufs(); CHECK(lufs < -20.0);
    CHECK(std::abs(p.autoGainDb() - std::min(12.0, -14.0 - lufs)) < 0.7);
    auto q = make({{Ride, 2}}); run(q, noise(-10, 40.0, 3)); CHECK(q.autoGainDb() < -3.0);
    // speed: after 4 s the slow ride has moved about 2 dB, the fast one about 12 dB (limited by the target)
    auto s1 = make({{Ride, 0}, {MaxBoost, 12}}); run(s1, noise(-30, 5.0, 3)); auto s2 = make({{Ride, 2}, {MaxBoost, 12}}); run(s2, noise(-30, 5.0, 3));
    CHECK(s2.autoGainDb() > s1.autoGainDb() + 4.0); CHECK(s1.autoGainDb() > 0.5); CHECK(s1.autoGainDb() < 3.0);
}
TEST_CASE("LV06 Max boost, Custom target, and silence holds the gain") {
    auto p = make({{Ride, 2}, {MaxBoost, 3}}); run(p, noise(-45, 30.0, 4)); CHECK(p.autoGainDb() <= 3.0001);
    auto c = make({{Ride, 2}, {Target, 3}, {Custom, -20}, {MaxBoost, 12}}); run(c, noise(-30, 40.0, 3)); CHECK(std::abs((p.loudnessLufs() * 0 + c.loudnessLufs() + c.autoGainDb()) - (-20.0)) < 0.7);
    auto d = make({{Ride, 2}, {MaxBoost, 12}}); run(d, noise(-30, 30.0, 3)); const double g = d.autoGainDb(); run(d, std::vector<float>(48000 * 20, 0.0f)); CHECK(std::abs(d.autoGainDb() - g) < 2.5); CHECK(d.deadAir());
}
TEST_CASE("LV06 the limiter holds the ceiling") {
    auto p = make({{Ceiling, -3}}); std::vector<float> x = noise(6, 4.0, 5); for (auto& v : x) v *= 3.0f;
    for (float v : run(p, x)) { REQUIRE(std::isfinite(v)); REQUIRE(std::abs(v) <= 0.70795f + 1e-6f); }
    auto q = make({{Ceiling, 0}}); for (float v : run(q, x)) REQUIRE(std::abs(v) <= 1.0f + 1e-6f);
}
TEST_CASE("LV06 Mono safe: bass goes mono, out-of-phase sides are pulled in") {
    auto p = make({{MonoSafe, 1}, {Target, 3}, {Custom, -5}}); auto l = sine(-20, 3.0, 60), r = l; for (auto& v : r) v = -v;   // 60 Hz, opposite polarity
    const auto o = run2(p, l, r); CHECK(rmsDb(o.first, 100000, 144000) < -20.0 - 25.0);
    auto q = make({{MonoSafe, 1}}); auto a = sine(-20, 3.0, 2000), b = a; for (auto& v : b) v = -v; const auto o2 = run2(q, a, b); auto z0 = make({{MonoSafe, 0}}); const auto o0 = run2(z0, a, b); CHECK(rmsDb(o2.first, 100000, 144000) < rmsDb(o0.first, 100000, 144000) - 8.0);
    auto z = make({{MonoSafe, 0}}); const auto o3 = run2(z, a, b); CHECK(std::abs(rmsDb(o3.first, 100000, 144000) - (-20.0)) < 1.0);
}
TEST_CASE("LV06 mono, odd blocks, before prepare, silence") {
    auto p = make(); std::vector<float> l = noise(-20, 2.0, 5); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; p.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f);
    auto q = make(); for (float v : run(q, std::vector<float>(48000, 0.0f))) REQUIRE(v == 0.0f);
}
