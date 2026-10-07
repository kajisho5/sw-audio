#include "doctest.h"
#include "lv09/lv09.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv09;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// hum: 50 Hz and its first `h` multiples at -30 dBFS each, plus a 1 kHz "voice" at -30
std::vector<float> hum(double seconds, double f0, int h, double lvl = -30) {
    auto y = sine(-30, seconds, 1000); for (int k = 1; k <= h; ++k) { const auto t = sine(lvl, seconds, f0 * k); for (size_t i = 0; i < y.size(); ++i) y[i] += t[i]; } return y;
}
}

TEST_CASE("LV09 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Base].labels == std::vector<std::string>{"50 Hz", "60 Hz", "Auto"}); CHECK(s[Base].def == 2);
    CHECK(s[Harmonics].steps.size() == 16); CHECK(s[Harmonics].steps.front() == 1); CHECK(s[Harmonics].steps.back() == 16); CHECK(s[Harmonics].def == 8);
    CHECK(s[Depth].min == -40); CHECK(s[Depth].max == 0); CHECK(s[Depth].def == -30);
    CHECK(s[Width].labels == std::vector<std::string>{"Narrow", "Medium", "Wide"}); CHECK(s[Width].def == 0);
    CHECK(s[TrackDrift].def == 1); CHECK(s[Listen].automatable == false); CHECK(s[Listen].def == 0);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV09 cuts the hum and its harmonics, keeps the voice") {
    auto p = make({{Base, 0}, {Harmonics, 8}, {TrackDrift, 0}}); const auto x = hum(6.0, 50, 8), y = run(p, x);
    for (int k : {1, 2, 5, 8}) CHECK(binDb(y, 50.0 * k, 3 * 48000, 6 * 48000) < binDb(x, 50.0 * k, 3 * 48000, 6 * 48000) - 20.0);
    CHECK(std::abs(binDb(y, 1000, 3 * 48000, 6 * 48000) - binDb(x, 1000, 3 * 48000, 6 * 48000)) < 0.5);
}
TEST_CASE("LV09 Harmonics counts exactly: 1 and 3 notches") {
    auto p1 = make({{Base, 0}, {Harmonics, 1}, {TrackDrift, 0}}); const auto x = hum(6.0, 50, 4), y = run(p1, x);
    CHECK(binDb(y, 50, 3 * 48000, 6 * 48000) < binDb(x, 50, 3 * 48000, 6 * 48000) - 20.0); CHECK(binDb(y, 100, 3 * 48000, 6 * 48000) > binDb(x, 100, 3 * 48000, 6 * 48000) - 1.0);
    auto p3 = make({{Base, 0}, {Harmonics, 3}, {TrackDrift, 0}}); const auto z = run(p3, x);
    CHECK(binDb(z, 150, 3 * 48000, 6 * 48000) < binDb(x, 150, 3 * 48000, 6 * 48000) - 20.0); CHECK(binDb(z, 200, 3 * 48000, 6 * 48000) > binDb(x, 200, 3 * 48000, 6 * 48000) - 1.0);
}
TEST_CASE("LV09 Depth sets how deep; 60 Hz base; Auto finds 60 Hz") {
    const auto x = hum(6.0, 60, 4);
    auto d = make({{Base, 1}, {Depth, -12}, {TrackDrift, 0}}); const auto y = run(d, x); const double cut = binDb(x, 60, 3 * 48000, 6 * 48000) - binDb(y, 60, 3 * 48000, 6 * 48000); CHECK(std::abs(cut - 12.0) < 2.5);
    auto a = make({{Base, 2}}); const auto z = run(a, hum(10.0, 60, 4)); CHECK(binDb(z, 60, 6 * 48000, 10 * 48000) < binDb(x, 60, 3 * 48000, 6 * 48000) - 15.0); CHECK(std::abs(a.humHz() - 60.0) < 1.0);
}
TEST_CASE("LV09 Track drift follows a hum that is off by 1.5 Hz") {
    const auto x = hum(14.0, 51.5, 3);
    auto on = make({{Base, 0}, {Harmonics, 3}, {TrackDrift, 1}}); const auto a = run(on, x); auto off = make({{Base, 0}, {Harmonics, 3}, {TrackDrift, 0}}); const auto b = run(off, x);
    CHECK(binDb(a, 51.5, 10 * 48000, 14 * 48000) < binDb(b, 51.5, 10 * 48000, 14 * 48000) - 6.0);
}
TEST_CASE("LV09 Listen is the removed part") {
    const auto x = hum(6.0, 50, 4); auto p = make({{Base, 0}, {TrackDrift, 0}, {Listen, 1}}); const auto y = run(p, x);
    CHECK(binDb(y, 50, 3 * 48000, 6 * 48000) > binDb(x, 50, 3 * 48000, 6 * 48000) - 3.0); CHECK(binDb(y, 1000, 3 * 48000, 6 * 48000) < binDb(x, 1000, 3 * 48000, 6 * 48000) - 12.0);   // the comb bends the phase a little around the voice, so the residue there is not zero
    auto q = make({{Base, 0}, {TrackDrift, 0}}); const auto z = run(q, x); for (size_t i = 100000; i < 100200; ++i) NEAR(x[i], y[i] + z[i], 1e-4);
}
TEST_CASE("LV09 finite, mono, odd blocks, silence, before prepare") {
    auto p = make(); auto l = hum(2.0, 50, 4); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; p.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    auto q = make(); for (float v : run(q, std::vector<float>(48000, 0.0f))) REQUIRE(v == 0.0f);
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f);
}
