#include "doctest.h"
#include "rs03/rs03.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::rs03;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// hum: fundamental f and the multiples 1..n at -30 dBFS each, plus a 1 kHz tone at -20 dBFS
std::vector<float> hum(double f, int n, double seconds, bool tone = true) {
    std::vector<float> y = tone ? sine(-20, seconds, 1000) : std::vector<float>(static_cast<size_t>(seconds * kFs), 0.0f);
    for (int h = 1; h <= n; ++h) { const auto s = sine(-30, seconds, f * h); for (size_t i = 0; i < y.size(); ++i) y[i] += s[i]; }
    return y;
}
double atten(const std::vector<float>& y, const std::vector<float>& x, double f, double secFrom, double secTo) {
    const size_t a = static_cast<size_t>(secFrom * kFs), b = static_cast<size_t>(secTo * kFs); return binDb(y, f, a, b) - binDb(x, f, a, b);
}
}

TEST_CASE("RS03 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"rs03.base", "rs03.harmonics", "rs03.depth", "rs03.width", "rs03.buzz", "rs03.evo.on"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Base].labels == std::vector<std::string>{"50", "60", "Auto"}); CHECK(s[Base].def == 0);
    CHECK(s[Harmonics].labels == std::vector<std::string>{"2", "4", "8", "16"}); CHECK(s[Harmonics].def == 8);
    CHECK(s[Depth].min == 0); CHECK(s[Depth].max == 10); CHECK(s[Depth].def == 5);
    CHECK(s[Width].def == 0); CHECK(std::string(s[Width].minLabel) == "Narrow"); CHECK(std::string(s[Width].maxLabel) == "Wide");
    CHECK(s[Buzz].min == 0); CHECK(s[Buzz].max == 10); CHECK(s[Buzz].def == 0);
    CHECK(s[Track].labels == std::vector<std::string>{"Off", "On"}); CHECK(s[Track].def == 1);
}
TEST_CASE("RS03 no delay; silence is silence; Depth 0 leaves the signal") {
    Processor q; CHECK(q.latencySamples() == 0);
    { auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f); }
    auto p = make({{Depth, 0}, {Track, 0}}); const auto x = hum(50, 6, 2.0); const auto y = run(p, x);
    for (size_t i = 24000; i < y.size(); i += 101) NEAR(y[i], x[i], 1e-3);
}
TEST_CASE("RS03 Depth 10 takes the hum and its harmonics down; the 1 kHz tone stays") {
    auto p = make({{Depth, 10}, {Track, 0}}); const auto x = hum(50, 8, 4.0); const auto y = run(p, x);
    for (int h = 1; h <= 8; ++h) CHECK(atten(y, x, 50.0 * h, 2.0, 4.0) < -30.0);
    NEAR(atten(y, x, 1000, 2.0, 4.0), 0.0, 0.5);
    { auto q = make({{Depth, 10}, {Track, 0}}); const auto s75 = sine(-20, 3.0, 75); NEAR(atten(run(q, s75), s75, 75, 1.0, 3.0), 0.0, 1.0); }   // between two notches: nothing
}
TEST_CASE("RS03 Depth scales the notch: 4 dB a step") {
    for (double d : {2.5, 5.0, 7.5}) { auto p = make({{Depth, d}, {Track, 0}}); const auto x = hum(50, 2, 4.0); const auto y = run(p, x); NEAR(atten(y, x, 100, 2.0, 4.0), -4.0 * d, 1.5); }
}
TEST_CASE("RS03 Harmonics: how many notches") {
    auto p = make({{Harmonics, 2}, {Depth, 10}, {Track, 0}}); const auto x = hum(50, 4, 4.0); const auto y = run(p, x);
    CHECK(atten(y, x, 50, 2.0, 4.0) < -30.0); CHECK(atten(y, x, 100, 2.0, 4.0) < -30.0);
    NEAR(atten(y, x, 150, 2.0, 4.0), 0.0, 1.0); NEAR(atten(y, x, 200, 2.0, 4.0), 0.0, 1.0);
    auto q = make({{Harmonics, 16}, {Depth, 10}, {Track, 0}}); const auto x2 = hum(50, 16, 4.0, false); const auto y2 = run(q, x2);
    CHECK(atten(y2, x2, 800, 2.0, 4.0) < -25.0);
}
TEST_CASE("RS03 Base 60 and Auto") {
    auto p = make({{Base, Hz60}, {Depth, 10}, {Track, 0}}); const auto x = hum(60, 4, 4.0); const auto y = run(p, x);
    for (int h = 1; h <= 4; ++h) CHECK(atten(y, x, 60.0 * h, 2.0, 4.0) < -30.0);
    { auto q = make({{Base, Hz60}, {Depth, 10}, {Track, 0}}); const auto s50 = sine(-20, 3.0, 50); NEAR(atten(run(q, s50), s50, 50, 1.0, 3.0), 0.0, 1.0); }   // 50 Hz is not touched
    auto a = make({{Base, Auto}, {Depth, 10}}); const auto x2 = hum(60, 4, 8.0); const auto y2 = run(a, x2);
    for (int h = 1; h <= 4; ++h) CHECK(atten(y2, x2, 60.0 * h, 5.0, 8.0) < -25.0);
}
TEST_CASE("RS03 Width: a wider notch also takes a hum a little off the centre") {
    const auto x = hum(50.8, 1, 4.0, false);
    auto narrow = make({{Width, 0}, {Depth, 10}, {Track, 0}}); auto wide = make({{Width, 100}, {Depth, 10}, {Track, 0}});
    const double an = atten(run(narrow, x), x, 50.8, 2.0, 4.0), aw = atten(run(wide, x), x, 50.8, 2.0, 4.0);
    CHECK(aw < an - 10.0); CHECK(aw < -25.0);
}
TEST_CASE("RS03 Track follows a hum that is not at the base frequency") {
    const auto x = hum(51.5, 4, 12.0);
    auto on = make({{Depth, 10}, {Track, 1}}); auto off = make({{Depth, 10}, {Track, 0}});
    const auto yon = run(on, x), yoff = run(off, x);
    CHECK(atten(yon, x, 51.5, 9.0, 12.0) < -25.0); CHECK(atten(yoff, x, 51.5, 9.0, 12.0) > -15.0);
    NEAR(on.humHz(), 51.5, 0.3);
    // the hum wanders: 50.0 -> 51.0 Hz over 12 s
    auto p = make({{Depth, 10}, {Track, 1}}); std::vector<float> w(static_cast<size_t>(12 * kFs), 0.0f); double ph = 0; for (size_t i = 0; i < w.size(); ++i) { const double f = 50.0 + 1.0 * static_cast<double>(i) / static_cast<double>(w.size()); ph += 2 * kPi * f / kFs; w[i] = static_cast<float>(0.03 * std::sin(ph) + 0.03 * std::sin(2 * ph)); }
    const auto yw = run(p, w); double e0 = 0, e1 = 0; for (size_t i = 9 * 48000; i < w.size(); ++i) { e0 += double(w[i]) * w[i]; e1 += double(yw[i]) * yw[i]; }
    CHECK(10 * std::log10(e1 / e0) < -15.0);
}
TEST_CASE("RS03 Buzz: more notches above Harmonics, and the odd multiples deeper") {
    auto p = make({{Harmonics, 8}, {Depth, 5}, {Buzz, 0}, {Track, 0}}), q = make({{Harmonics, 8}, {Depth, 5}, {Buzz, 10}, {Track, 0}});
    const auto x = hum(50, 12, 4.0, false); const auto y0 = run(p, x), y1 = run(q, x);
    NEAR(atten(y0, x, 450, 2.0, 4.0), 0.0, 1.5);          // the 9th is outside Harmonics 8
    CHECK(atten(y1, x, 450, 2.0, 4.0) < -25.0);           // Buzz reaches it
    CHECK(atten(y1, x, 150, 2.0, 4.0) < atten(y0, x, 150, 2.0, 4.0) - 6.0);   // odd multiples deeper
    NEAR(atten(y1, x, 100, 2.0, 4.0), atten(y0, x, 100, 2.0, 4.0), 1.5);      // the even ones are not
}
TEST_CASE("RS03 loud input stays finite") {
    auto p = make({{Depth, 10}, {Buzz, 10}, {Harmonics, 16}}); auto x = noise(6, 2.0, 9); for (auto& v : x) v *= 8.0f; for (float v : run(p, x)) CHECK(std::isfinite(v));
}
