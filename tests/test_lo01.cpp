#include "doctest.h"
#include "lo01/lo01.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lo01;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> tone(double amp, double f, double sec) {
    std::vector<float> x(static_cast<size_t>(sec * kFs)); for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(amp * std::sin(2 * kPi * f * static_cast<double>(i) / kFs)); return x;
}
double at(const std::vector<float>& y, double f) { return binDb(y, f, y.size() / 2, y.size()); }
Set base(double harm, double width = 2) { return {{Harmonics, harm}, {Width, width}, {Original, 0}, {Preview, 0}}; }
}

TEST_CASE("LO01 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"lo01.frequency", "lo01.harmonics", "lo01.original", "lo01.width", "lo01.preview"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Frequency].min == 40); CHECK(s[Frequency].max == 200); CHECK(s[Frequency].def == 80); CHECK(s[Frequency].curve == Curve::Log);
    CHECK(s[Harmonics].max == 100); CHECK(s[Harmonics].def == 30);
    CHECK(s[Original].min == -24); CHECK(s[Original].max == 0); CHECK(s[Original].def == 0);
    CHECK(s[Width].labels == std::vector<std::string>{"Narrow", "Medium", "Wide"}); CHECK(s[Width].def == 0);
    CHECK(s[Preview].labels == std::vector<std::string>{"Off", "Phone safe", "Club"}); CHECK(s[Preview].def == 0); CHECK(!s[Preview].automatable);
}
TEST_CASE("LO01 no delay; with Harmonics 0 and Original 0 dB the level is flat") {
    Processor p; CHECK(p.latencySamples() == 0);
    for (double f : {40.0, 80.0, 200.0, 1000.0, 8000.0}) {
        auto q = make(base(0)); const auto x = tone(0.3, f, 1.0); const auto y = run(q, x);
        NEAR(at(y, f) - at(x, f), 0.0, 0.1);
    }
}
TEST_CASE("LO01 Harmonics 100 % Wide: orders 2..5, none above, level relative to the low band") {
    auto p = make(base(100)); const auto x = tone(0.3, 50, 2.0); const auto y = run(p, x);
    const double low = at(x, 50) + 20 * std::log10(0.867);   // LR4 low-pass at 80 Hz, 50 Hz
    const double w[4] = {1.0, 0.7, 0.5, 0.35}; const double norm = std::sqrt(1 + 0.49 + 0.25 + 0.1225);
    for (int k = 2; k <= 5; ++k) NEAR(at(y, 50.0 * k) - low, 20 * std::log10(w[k - 2] / norm), 1.5);
    CHECK(at(y, 300) < at(y, 100) - 25.0);
    NEAR(at(y, 50) - at(x, 50), 0.0, 0.6);   // the original stays
}
TEST_CASE("LO01 Width sets the highest order") {
    const auto x = tone(0.3, 50, 2.0);
    auto n = make(base(100, 0)); const auto yn = run(n, x);
    CHECK(at(yn, 100) > at(yn, 50) - 12.0); CHECK(at(yn, 150) > at(yn, 50) - 15.0); CHECK(at(yn, 200) < at(yn, 100) - 30.0);
    auto m = make(base(100, 1)); const auto ym = run(m, x);
    CHECK(at(ym, 200) > at(ym, 100) - 12.0); CHECK(at(ym, 250) < at(ym, 100) - 25.0);
    auto w = make(base(100, 2)); const auto yw = run(w, x);
    CHECK(at(yw, 250) > at(yw, 100) - 14.0);
}
TEST_CASE("LO01 harmonic level follows Harmonics; the total stays the same across Width") {
    const auto x = tone(0.3, 50, 2.0);
    auto a = make(base(100)); auto b = make(base(30)); auto c = make(base(100, 0));
    const auto ya = run(a, x), yb = run(b, x), yc = run(c, x);
    NEAR(at(yb, 100) - at(ya, 100), 20 * std::log10(0.3), 1.0);
    auto energy = [](const std::vector<float>& y) { double e = 0; for (int k = 2; k <= 5; ++k) e += std::pow(10.0, at(y, 50.0 * k) / 10.0); return 10 * std::log10(e); };
    NEAR(energy(yc), energy(ya), 1.0);
}
TEST_CASE("LO01 the harmonic amount does not depend on the input level") {
    auto a = make(base(100)); auto b = make(base(100));
    const auto ya = run(a, tone(0.3, 50, 2.0)), yb = run(b, tone(0.03, 50, 2.0));
    NEAR(at(ya, 100) - at(ya, 50), at(yb, 100) - at(yb, 50), 1.0);
    NEAR(at(ya, 150) - at(ya, 50), at(yb, 150) - at(yb, 50), 1.0);
}
TEST_CASE("LO01 Original lowers the low band only") {
    auto a = make(base(0)); Set s = base(0); s.push_back({Original, -12}); auto b = make(s);
    const auto lo = tone(0.3, 40, 1.0), hi = tone(0.3, 1000, 1.0);
    const double d = at(run(a, lo), 40) - at(run(b, lo), 40);
    CHECK(d > 9.5); CHECK(d < 12.5);
    NEAR(at(run(a, hi), 1000), at(run(b, hi), 1000), 0.05);
}
TEST_CASE("LO01 Frequency moves the split (a 150 Hz tone is a harmonic source at 200 Hz, not at 80 Hz)") {
    const auto x = tone(0.3, 150, 2.0);
    Set lo = base(100); lo.push_back({Frequency, 80}); Set hi = base(100); hi.push_back({Frequency, 200});
    auto a = make(lo); auto b = make(hi);
    const auto ya = run(a, x), yb = run(b, x);
    CHECK(at(yb, 300) > at(ya, 300) + 15.0);
}
TEST_CASE("LO01 Preview Phone safe: lows removed, a small resonance, harmonics carry the bass") {
    const auto lo = tone(0.3, 60, 2.0), mid = tone(0.3, 1000, 1.0);
    auto off = make(base(100)); Set ps = base(100); ps.push_back({Preview, 1}); auto ph = make(ps);
    CHECK(rmsDb(run(off, lo)) > -25.0);
    Set ps0 = base(0); ps0.push_back({Preview, 1}); auto ph0 = make(ps0);
    const auto without = run(ph0, lo), with = run(ph, lo);
    CHECK(rmsDb(with) > rmsDb(without) + 15.0);
    auto m0 = make(base(0)); Set pm = base(0); pm.push_back({Preview, 1}); auto m1 = make(pm);
    const double g = at(run(m1, mid), 1000) - at(run(m0, mid), 1000);
    CHECK(g > -1.0); CHECK(g < 4.0);
    CHECK(at(without, 60) < at(lo, 60) - 30.0);
}
TEST_CASE("LO01 Preview Club keeps the lows and removes only the sub-bass") {
    Set cs = base(0); cs.push_back({Preview, 2}); auto c = make(cs); auto o = make(base(0));
    const auto l50 = tone(0.3, 50, 1.0), l15 = tone(0.3, 15, 2.0);
    NEAR(at(run(c, l50), 50), at(run(o, l50), 50), 0.8);
    const auto a = run(c, l15), b = run(o, l15); CHECK(at(a, 15) < at(b, 15) - 10.0);
}
TEST_CASE("LO01 silence, DC and the channels are handled") {
    auto p = make(base(100)); std::vector<float> z(24000, 0.0f); const auto y = run(p, z); for (float v : y) CHECK(v == 0.0f);
    auto q = make(base(100)); std::vector<float> dc(96000, 0.5f); const auto d = run(q, dc);
    NEAR(d.back(), 0.5, 0.02);
    auto r = make(base(100)); const auto l = tone(0.3, 50, 1.0); std::vector<float> sil(l.size(), 0.0f);
    const auto o = run2(r, l, sil); for (float v : o.second) CHECK(v == 0.0f);
    CHECK(rmsDb(o.first) > -30.0);
}
TEST_CASE("LO01 the highs get no harmonics") {
    auto p = make(base(100)); const auto x = tone(0.3, 10000, 1.0); const auto y = run(p, x);
    NEAR(at(y, 10000) - at(x, 10000), 0.0, 0.1);
    CHECK(at(y, 20000) < -90.0); CHECK(at(y, 100) < -70.0);
}
