#include "doctest.h"
#include "lv02/lv02.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv02;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// a steady "howl" tone at f (-30 dBFS) in a quiet room
std::vector<float> howl(double f, double seconds, double lvl = -30, unsigned seed = 3) { auto a = sine(lvl, seconds, f), n = noise(-70, seconds, seed); for (size_t i = 0; i < a.size(); ++i) a[i] += n[i]; return a; }
}

TEST_CASE("LV02 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Sensitivity].labels == std::vector<std::string>{"Low", "Mid", "High"}); CHECK(s[Sensitivity].def == 2);
    CHECK(s[MaxDepth].min == -24); CHECK(s[MaxDepth].max == -3); CHECK(s[MaxDepth].def == -12);
    CHECK(s[Width].min == doctest::Approx(1.0 / 20)); CHECK(s[Width].max == doctest::Approx(1.0 / 3)); CHECK(s[Width].def == doctest::Approx(0.1));
    CHECK(s[Release].min == 1); CHECK(s[Release].max == 60); CHECK(s[Release].def == 8);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV02 a steady howl gets a LIVE filter and is cut; the room is not") {
    auto p = make({{Release, 30}}); const auto x = howl(2500, 6.0), y = run(p, x);
    CHECK(p.guard().activeCount() == 1);
    CHECK(std::abs(p.guard().slot(0).freq - 2500) < 25);
    CHECK(rmsDb(y, 5 * 48000, 6 * 48000) < rmsDb(x, 5 * 48000, 6 * 48000) - 6.0);
}
TEST_CASE("LV02 Max depth limits the cut") {
    auto p = make({{MaxDepth, -6}, {Release, 60}}); const auto x = howl(1800, 8.0), y = run(p, x);
    const double cut = rmsDb(x, 6 * 48000, 8 * 48000) - rmsDb(y, 6 * 48000, 8 * 48000); CHECK(cut > 3.0); CHECK(cut < 7.5);
}
TEST_CASE("LV02 sensitivity: Low waits longer than High") {
    auto count = [&](double sens, double seconds) { auto p = make({{Sensitivity, sens}}); run(p, howl(3000, seconds)); return p.guard().activeCount(); };
    CHECK(count(2, 0.25) == 1); CHECK(count(0, 0.25) == 0); CHECK(count(0, 1.5) == 1);
}
TEST_CASE("LV02 release: LIVE filters fade out when the howl is gone; FIXED stay") {
    auto p = make({{Release, 1}}); run(p, howl(2200, 3.0)); CHECK(p.guard().activeCount() == 1);
    run(p, noise(-70, 3.0, 4)); CHECK(p.guard().activeCount() == 0);
    auto q = make({{Release, 1}}); run(q, howl(2200, 3.0)); q.lockFilters(); run(q, noise(-70, 4.0, 4)); CHECK(q.guard().activeCount() == 1);
    q.clearLive(); CHECK(q.guard().activeCount() == 1);   // locked ones are not LIVE
}
TEST_CASE("LV02 Ring out stores FIXED filters at once; clear live removes only LIVE") {
    auto p = make({{Sensitivity, 0}}); p.ringOut(true); run(p, howl(1500, 1.0)); p.ringOut(false);
    CHECK(p.guard().activeCount() == 1); CHECK(p.guard().slot(0).fixed); CHECK(p.guard().slot(0).targetDb == 12);
    run(p, howl(4000, 5.0)); CHECK(p.guard().activeCount() == 2); p.clearLive(); run(p, noise(-70, 2.0, 5)); CHECK(p.guard().activeCount() == 1);
}
TEST_CASE("LV02 harmonic series of music is not cut") {
    std::vector<float> m = sine(-30, 6.0, 440); const auto h2 = sine(-36, 6.0, 880), h3 = sine(-38, 6.0, 1320), h4 = sine(-40, 6.0, 1760); for (size_t i = 0; i < m.size(); ++i) m[i] += h2[i] + h3[i] + h4[i];
    auto p = make({{Release, 30}}); run(p, m); const auto& g = p.guard(); int fixedHarm = 0; for (int i = 0; i < g.slots(); ++i) if (g.slot(i).used && g.slot(i).freq > 600) ++fixedHarm;
    CHECK(fixedHarm == 0);
}
TEST_CASE("LV02 speech-like changing tones do not trigger; state keeps FIXED filters") {
    auto p = make({{Sensitivity, 1}}); std::vector<float> s(static_cast<size_t>(6 * kFs)); double ph = 0;
    for (size_t i = 0; i < s.size(); ++i) { const double f = 300 + 80 * std::sin(2 * kPi * 3.0 * static_cast<double>(i) / kFs); ph += 2 * kPi * f / kFs; s[i] = static_cast<float>(0.03 * std::sin(ph)); }
    run(p, s); CHECK(p.guard().activeCount() == 0);
    auto q = make(); q.addFixed(2000, 10); q.addFixed(5000, 8); std::vector<uint8_t> st; q.saveExtra(st);
    auto r = make(); r.loadExtra(st.data(), st.size()); CHECK(r.guard().activeCount() == 2); CHECK(r.guard().fixedList().size() == 2);
    Processor u; u.loadExtra(st.data(), st.size()); u.prepare(kFs, 256); CHECK(u.guard().activeCount() == 2);
    std::vector<uint8_t> bad = {5, 1, 2}; r.loadExtra(bad.data(), bad.size());
}
TEST_CASE("LV02 finite output, mono, odd blocks, loud input, silence") {
    auto p = make(); auto l = howl(2000, 3.0, -6); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; p.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    auto q = make(); for (float v : run(q, std::vector<float>(96000, 0.0f))) REQUIRE(v == 0.0f);
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f);
}
