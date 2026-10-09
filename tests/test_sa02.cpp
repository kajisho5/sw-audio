#include "doctest.h"
#include "sa02/sa02.hpp"
#include "tu.hpp"
#include "os_helpers.hpp"
using namespace sw;
using namespace sw::sa02;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}, int seed = 4242) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.setSeed(seed); p.prepare(kFs, 256); p.snapToTargets(); return p; }
double h(Set s, int k, double f = 1000, double db = -12) { auto p = make(s); return harmDb(run(p, sine(db, 2, f)), f, k); }
}

TEST_CASE("SA02 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"sa02.color", "sa02.drive", "sa02.crosstalk", "sa02.noise", "sa02.width", "sa02.output", "sa02.group", "sa02.os"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Color].labels == std::vector<std::string>{"Iron", "Clean", "Punch", "Vint"}); CHECK(s[Color].def == 0);
    CHECK(s[Drive].max == 10); CHECK(s[Drive].def == 2); CHECK(s[Crosstalk].max == 10); CHECK(s[Crosstalk].def == 0);
    CHECK(std::string(s[Noise].minLabel) == "Off"); CHECK(std::string(s[Noise].maxLabel) == "Max"); CHECK(s[Noise].def == 0);
    CHECK(s[Width].min == 0); CHECK(s[Width].max == 150); CHECK(s[Width].def == 100);
    CHECK(s[Output].min == -10); CHECK(s[Output].max == 10);
    CHECK(s[Group].min == 1); CHECK(s[Group].max == 8); CHECK(s[Group].def == 1); CHECK(s[Group].steps.size() == 8);
}
TEST_CASE("SA02 colours: Clean is cleaner, Iron works the lows harder, Vint leans on even harmonics") {
    const double drive = 8;
    CHECK(h({{Color, 1}, {Drive, drive}}, 3) < h({{Color, 0}, {Drive, drive}}, 3) - 4.0);
    CHECK(h({{Color, 0}, {Drive, drive}}, 3, 60) > h({{Color, 0}, {Drive, drive}}, 3, 1000) + 6.0);
    CHECK(h({{Color, 3}, {Drive, drive}}, 2) > h({{Color, 3}, {Drive, drive}}, 3));
    CHECK(h({{Color, 0}, {Drive, 10}}, 3) > h({{Color, 0}, {Drive, 2}}, 3) + 6.0);
}
TEST_CASE("SA02 Crosstalk: -80 dB at 0, about -40 dB at 10") {
    auto leak = [](double ct) { auto p = make({{Crosstalk, ct}, {Drive, 0}}, 1); auto [l, r] = run2(p, sine(-20, 2, 1000), std::vector<float>(96000, 0.0f)); return rmsDb(r) - rmsDb(l); };
    CHECK(leak(0) < -70.0); NEAR(leak(10), -40.0, 2.5); CHECK(leak(5) > leak(0)); CHECK(leak(10) > leak(5));
}
TEST_CASE("SA02 Noise: Off is silence, Max is the console floor") {
    auto floorDb = [](double n) { auto p = make({{Noise, n}}); return rmsDb(run(p, std::vector<float>(96000, 0.0f))); };
    { auto p = make(); for (float v : run(p, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f); }
    NEAR(floorDb(1), -70.0, 3.0); CHECK(floorDb(0.5) < floorDb(1) - 5.0);
}
TEST_CASE("SA02 Width: 0 = mono, 150 % widens the sides") {
    auto side = [](double w) { auto p = make({{Width, w}, {Drive, 0}}, 7); const auto t = sine(-20, 2, 800); std::vector<float> r(t.size()); for (size_t i = 0; i < t.size(); ++i) r[i] = -t[i]; auto [yl, yr] = run2(p, t, r); return rmsDb(yl); };
    CHECK(side(0) < -40.0);   // not exactly mono: each channel has its own gain deviation (a few hundredths of a dB) NEAR(side(150) - side(100), 3.5, 0.5);
}
TEST_CASE("SA02 each instance is a little different, from its seed: reproducible, bounded, different between seeds") {
    auto lvl = [](double seed) { auto p = make({{Drive, 0}}, static_cast<int>(seed)); const auto y = run(p, sine(-20, 2, 1000)); return rmsDb(y); };
    CHECK(lvl(11) == lvl(11));
    CHECK(std::abs(lvl(11) - lvl(12)) > 0.01);
    for (double s : {11.0, 12.0, 13.0, 777.0, 9000.0}) NEAR(lvl(s), -20.0, 0.8);
    // a new instance has a seed of its own, and it is saved / restored with the project
    Processor p1, p2; CHECK(p1.seed() >= 1); CHECK(p1.seed() != p2.seed());
    std::vector<uint8_t> blob; p1.saveExtra(blob); p2.loadExtra(blob.data(), blob.size()); CHECK(p2.seed() == p1.seed());
}
TEST_CASE("SA02 Group: instances in the same group load each other, other groups do not") {
    auto quiet = [](int neighbourGroup) {
        Processor a; a.setParam(Group, 1); a.setSeed(5); a.setParam(Drive, 6); a.prepare(kFs, 256); a.snapToTargets();
        Processor b; b.setParam(Group, neighbourGroup); b.setSeed(6); b.setParam(Drive, 6); b.prepare(kFs, 256); b.snapToTargets();
        const auto loud = sine(-6, 3, 200), soft = sine(-20, 3, 1000); std::vector<float> ya(soft.size());
        for (size_t off = 0; off + 256 <= soft.size(); off += 256) {
            std::vector<float> lb(loud.begin() + off, loud.begin() + off + 256), rb = lb; float* cb[2] = {lb.data(), rb.data()}; b.process(cb, 2, 256);   // the loud neighbour publishes its level
            std::vector<float> la(soft.begin() + off, soft.begin() + off + 256), ra = la; float* ca[2] = {la.data(), ra.data()}; a.process(ca, 2, 256);
            std::copy(la.begin(), la.end(), ya.begin() + off);
        }
        return harmDb(ya, 1000, 3);
    };
    const double same = quiet(1), other = quiet(2);
    CHECK(same > other + 0.5);
}
TEST_CASE("SA02 extreme input finite, silence silent") {
    auto p = make({{Drive, 10}, {Crosstalk, 10}, {Width, 150}});
    for (float v : run(p, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f);
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : run(p, x)) REQUIRE(std::isfinite(v));
    CHECK(p.latencySamples() == 0);
}

// the common oversampling setting (spec 共通機能: 1x / 2x / 4x, default 2x)
TEST_CASE("SA02: the oversampling parameter is the last one, 1x / 2x / 4x, default 2x, and the shaper follows it") {
    const auto& s = specs();
    CHECK(s[Oversample].steps == std::vector<double>{1, 2, 4}); CHECK(s[Oversample].def == 2.0); CHECK(Oversample == kNumParams - 1);
    auto alias = [](int os) { auto p = make({{Drive, 10}, {Oversample, static_cast<double>(os)}}); return ost::relDb(p, 15000, 3000, 0.3); };
    const double a1 = alias(1), a2 = alias(2), a4 = alias(4);
    INFO("15 kHz, alias at 3 kHz: 1x " << a1 << " dB, 2x " << a2 << " dB, 4x " << a4 << " dB");
    CHECK(a1 > -50.0); CHECK(a2 < a1 - 15.0); CHECK(ost::notWorse(a4, a2));
}
