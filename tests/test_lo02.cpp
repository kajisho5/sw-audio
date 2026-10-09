#include "doctest.h"
#include "lo02/lo02.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lo02;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> tone(double amp, double f, double sec) {
    std::vector<float> x(static_cast<size_t>(sec * kFs)); for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(amp * std::sin(2 * kPi * f * static_cast<double>(i) / kFs)); return x;
}
double at(const std::vector<float>& y, double f) { return binDb(y, f, y.size() / 2, y.size()); }
// rising zero crossings (interpolated) in [a, b)
std::vector<double> rises(const std::vector<float>& y, size_t a, size_t b) {
    std::vector<double> t; for (size_t i = a + 1; i < b; ++i) if (y[i - 1] < 0 && y[i] >= 0) t.push_back(static_cast<double>(i) - 1 + (-y[i - 1]) / (y[i] - y[i - 1])); return t;
}
double freqOf(const std::vector<float>& y, size_t a, size_t b) { const auto t = rises(y, a, b); return t.size() < 2 ? 0.0 : (static_cast<double>(t.size()) - 1) * kFs / (t.back() - t.front()); }
Set subOnly(double sub = 10) { return {{Sub, sub}, {RangeHz, 90}, {Tune, 0}, {Punch, 0}, {Dry, 0}}; }
Set with(Set a, Set b) { a.insert(a.end(), b.begin(), b.end()); return a; }
}

TEST_CASE("LO02 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"lo02.sub", "lo02.rangehz", "lo02.tune", "lo02.punch", "lo02.dry", "lo02.unit"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Sub].max == 10); CHECK(s[Sub].def == 0);
    CHECK(s[RangeHz].steps == std::vector<double>{30, 45, 60, 90}); CHECK(s[RangeHz].def == 45);
    CHECK(s[Tune].min == -12); CHECK(s[Tune].max == 12); CHECK(s[Tune].def == 0);
    CHECK(s[Punch].max == 10); CHECK(s[Punch].def == 0);
    CHECK(s[Dry].def == 100); CHECK(std::string(s[Dry].minLabel) == "Off"); CHECK(std::string(s[Dry].maxLabel) == "Full");
}
TEST_CASE("LO02 no delay; Sub 0 and Dry Full leave the signal alone") {
    Processor p; CHECK(p.latencySamples() == 0);
    auto q = make(); const auto x = noise(-20, 1.0); const auto y = run(q, x); CHECK(y == x);
}
TEST_CASE("LO02 the sub is a pure sine one octave down, level set by Sub") {
    auto p = make(subOnly(10)); const auto x = tone(0.1, 60, 3.0); const auto y = run(p, x);
    const double want = 20 * std::log10(0.1 * 3.85);   // +12 dB: sub amplitude sqrt(3.98^2 - 1) x the band level
    NEAR(at(y, 30), want, 2.0);
    CHECK(at(y, 60) < at(y, 30) - 30.0); CHECK(at(y, 90) < at(y, 30) - 25.0);
    NEAR(freqOf(y, y.size() / 2, y.size()), 30.0, 0.3);
    auto h = make(subOnly(5)); const auto yh = run(h, x);
    CHECK(at(yh, 30) < at(y, 30) - 4.0); CHECK(at(yh, 30) > at(y, 30) - 12.0);
}
TEST_CASE("LO02 the sub's zero crossings sit on the bass's (phase locked, zero delay)") {
    auto p = make(subOnly(10)); const auto x = tone(0.1, 60, 3.0); const auto y = run(p, x);
    const size_t a = x.size() / 2, b = x.size();
    const auto tx = rises(x, a, b), ty = rises(y, a, b);
    REQUIRE(ty.size() > 10);
    for (double t : ty) { double best = 1e9; for (double u : tx) best = std::min(best, std::abs(u - t)); CHECK(best < 2.0); }
    for (size_t i = 1; i < ty.size(); ++i) NEAR(ty[i] - ty[i - 1], 1600.0, 4.0);
}
TEST_CASE("LO02 Tune moves the sub in semitones from the octave below") {
    const auto x = tone(0.1, 60, 4.0);
    for (auto [tune, hz] : std::vector<std::pair<int, double>>{{-12, 15.0}, {-5, 22.45}, {7, 44.9}, {12, 60.0}}) {
        auto s = subOnly(10); s.push_back({Tune, static_cast<double>(tune)}); auto p = make(s); const auto y = run(p, x);
        NEAR(freqOf(y, y.size() / 2, y.size()), hz, hz * 0.02);
    }
    Processor a = make({{Tune, 3.4}}), b = make({{Tune, 3}}); CHECK(run(a, x) == run(b, x));
}
TEST_CASE("LO02 it follows the pitch of the bass") {
    std::vector<float> x = tone(0.1, 55, 2.0); const auto x2 = tone(0.1, 41, 2.0); x.insert(x.end(), x2.begin(), x2.end());
    auto p = make(subOnly(10)); const auto y = run(p, x);
    NEAR(freqOf(y, static_cast<size_t>(1.5 * kFs), static_cast<size_t>(2.0 * kFs)), 27.5, 0.7);
    NEAR(freqOf(y, static_cast<size_t>(3.3 * kFs), x.size()), 20.5, 0.6);
}
TEST_CASE("LO02 a second harmonic in the bass does not double the sub") {
    std::vector<float> x(static_cast<size_t>(3 * kFs));
    for (size_t i = 0; i < x.size(); ++i) { const double t = static_cast<double>(i) / kFs; x[i] = static_cast<float>(0.1 * std::sin(2 * kPi * 45 * t) + 0.03 * std::sin(2 * kPi * 90 * t + 1.0)); }
    auto p = make(subOnly(10)); const auto y = run(p, x);
    NEAR(freqOf(y, x.size() / 2, x.size()), 22.5, 0.5);
}
TEST_CASE("LO02 Range limits the detection band") {
    const auto x = tone(0.1, 100, 3.0);   // above 2 x 30 Hz, below 2 x 90 Hz
    auto w = make(with(subOnly(10), {{RangeHz, 90}})); auto n = make(with(subOnly(10), {{RangeHz, 30}}));
    const auto yw = run(w, x), yn = run(n, x);
    CHECK(at(yw, 50) > -60.0);
    CHECK(at(yn, 50) < at(yw, 50) - 14.0);
}
TEST_CASE("LO02 Punch lifts the head of a note, not the sustain") {
    std::vector<float> x(static_cast<size_t>(0.3 * kFs), 0.0f); const auto t = tone(0.2, 60, 1.0); x.insert(x.end(), t.begin(), t.end());
    auto a = make(subOnly(10)); auto b = make(with(subOnly(10), {{Punch, 10}}));
    const auto ya = run(a, x), yb = run(b, x);
    const size_t on = static_cast<size_t>(0.3 * kFs);
    const double pa = peakDb(ya, on, on + static_cast<size_t>(0.15 * kFs)), pb = peakDb(yb, on, on + static_cast<size_t>(0.15 * kFs));
    CHECK(pb > pa + 2.5);
    NEAR(rmsDb(yb, x.size() - 24000, x.size()), rmsDb(ya, x.size() - 24000, x.size()), 0.5);
}
TEST_CASE("LO02 Dry sets the level of the input") {
    const auto x = tone(0.2, 3000, 1.0);
    auto o = make(with(subOnly(0), {{Dry, 0}})); auto h = make(with(subOnly(0), {{Dry, 50}}));
    CHECK(at(run(o, x), 3000) < -90.0);
    NEAR(at(run(h, x), 3000) - at(x, 3000), -6.02, 0.1);
}
TEST_CASE("LO02 silence, DC, very quiet input, and mono sum") {
    auto p = make(subOnly(10)); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
    auto q = make(with(subOnly(10), {{Dry, 100}})); std::vector<float> dc(96000, 0.5f); const auto d = run(q, dc); NEAR(d.back(), 0.5, 0.01);
    auto r = make(subOnly(10)); const auto quiet = run(r, tone(3e-4, 60, 2.0)); CHECK(peakDb(quiet, quiet.size() / 2, quiet.size()) < -85.0);
    auto s = make(subOnly(10)); const auto l = tone(0.1, 60, 2.0); std::vector<float> sil(l.size(), 0.0f);
    const auto o = run2(s, l, sil); CHECK(o.first == o.second);
}
