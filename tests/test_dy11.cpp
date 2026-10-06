#include "doctest.h"
#include "dy11/dy11.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::dy11;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
double level(Set s, double f, double db = -10, double sec = 3) { auto p = make(s); return rmsDb(run(p, sine(db, sec, f))); }
}

TEST_CASE("DY11 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* kn[] = {"mode", "freq", "thresh", "ratio", "attack", "release", "range", "gain", "width"};
    for (int n = 0; n < 6; ++n) for (int k = 0; k < 9; ++k) CHECK(std::string(s[static_cast<size_t>(band(n, k))].id) == "dy11.b" + std::to_string(n + 1) + "." + kn[k]);
    CHECK(std::string(s[Output].id) == "dy11.out"); CHECK(s[Output].min == -24); CHECK(s[Output].max == 24);
    const double fr[] = {60, 200, 600, 2000, 6000, 14000};
    for (int n = 0; n < 6; ++n) {
        CHECK(s[static_cast<size_t>(band(n, BMode))].labels == std::vector<std::string>{"Compress", "Expand", "Dynamic EQ"}); CHECK(s[static_cast<size_t>(band(n, BMode))].def == 0);
        const auto& f = s[static_cast<size_t>(band(n, BFreq))]; CHECK(f.min == 20); CHECK(f.max == 20000); CHECK(f.def == fr[n]); CHECK(f.curve == Curve::Log);
        CHECK(s[static_cast<size_t>(band(n, BThresh))].min == -60); CHECK(s[static_cast<size_t>(band(n, BThresh))].def == 0);
        CHECK(s[static_cast<size_t>(band(n, BRatio))].def == 2); CHECK(s[static_cast<size_t>(band(n, BRatio))].max == 20);
        const auto& a = s[static_cast<size_t>(band(n, BAttack))]; CHECK(a.def == 20); CHECK(a.skew == 3); CHECK(a.min == 0.1); CHECK(a.max == 200);
        const auto& r = s[static_cast<size_t>(band(n, BRelease))]; CHECK(r.def == 100); CHECK(r.skew == 3); CHECK(r.min == 5); CHECK(r.max == 3000);
        CHECK(s[static_cast<size_t>(band(n, BRange))].min == -24); CHECK(s[static_cast<size_t>(band(n, BRange))].def == -12);
        CHECK(s[static_cast<size_t>(band(n, BGain))].min == -12); CHECK(s[static_cast<size_t>(band(n, BGain))].max == 12);
        const auto& w = s[static_cast<size_t>(band(n, BWidth))]; CHECK(w.min == 0.1); CHECK(w.max == 4); CHECK(w.def == 1.0); CHECK(w.curve == Curve::Log);
    }
}
TEST_CASE("DY11 untouched at the defaults (the six dynamic filters are transparent when idle)") {
    for (double f : {40.0, 100.0, 300.0, 1000.0, 3000.0, 8000.0, 16000.0}) NEAR(level({}, f, -30, 1), -30.0, 0.3);
}
TEST_CASE("DY11 Compress: a wide band lowers what is inside it, up to Range, and leaves other bands alone") {
    const Set s = {{band(3, BThresh), -30}, {band(3, BRatio), 10}};   // band 4 (2 kHz)
    const double in = -10, ref = level({}, 2000), low = level({}, 200);
    CHECK(level(s, 2000) < ref - 8.0);
    CHECK(level(s, 2000) > ref - 13.5);                                                // Range -12
    NEAR(level(s, 200), low, 0.5);
    Set r6 = s; r6.push_back({band(3, BRange), -6});
    NEAR(level(r6, 2000), ref - 6.0, 1.5); (void)in;
}
TEST_CASE("DY11 Expand lowers what is below the threshold, by (ratio - 1) per dB, down to Range") {
    const Set s = {{band(3, BMode), 1}, {band(3, BThresh), -20}, {band(3, BRatio), 2}};
    NEAR(level(s, 2000, -40) - level({}, 2000, -40), -12.0, 2.0);      // 20 dB under: wants -20, Range -12
    NEAR(level(s, 2000, -10) - level({}, 2000, -10), 0.0, 0.5);        // above the threshold: untouched
    Set mild = s; mild.push_back({band(3, BRange), -24});
    NEAR(level(mild, 2000, -30) - level({}, 2000, -30), -10.0, 2.0);   // 10 dB under, ratio 2 -> -10
}
TEST_CASE("DY11 Dynamic EQ is a narrow bell (Width), Compress a wide one") {
    auto gr = [](int mode, double width, double f) {
        auto p = make({{band(4, BMode), static_cast<double>(mode)}, {band(4, BWidth), width}, {band(4, BThresh), -40}, {band(4, BRatio), 10}});   // band 5 at 6 kHz
        return rmsDb(run(p, sine(-10, 3, f))) - (-10.0);
    };
    CHECK(gr(2, 0.5, 6000) < -8.0);                                  // on the centre
    CHECK(gr(0, 1.0, 4000) < gr(2, 0.5, 4000) - 2.0);                // 4 kHz is inside the wide band, outside the narrow one
    CHECK(gr(2, 0.5, 3000) > -1.0);
}
TEST_CASE("DY11 Gain is a static boost / cut, modes can be mixed") {
    NEAR(level({{band(2, BGain), 6}}, 600, -30, 1) - level({}, 600, -30, 1), 6.0, 1.0);
    auto p = make({{band(1, BMode), 1}, {band(1, BThresh), -30}, {band(4, BMode), 2}, {band(4, BThresh), -30}, {band(5, BThresh), -30}});
    for (float v : run(p, noise(-12, 2))) REQUIRE(std::isfinite(v));
}
TEST_CASE("DY11 silence stays silent, extreme input finite, latency 0") {
    Set s; for (int n = 0; n < 6; ++n) { s.push_back({band(n, BThresh), -60}); s.push_back({band(n, BRatio), 20}); }
    auto p = make(s);
    for (float v : run(p, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f);
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : run(p, x)) REQUIRE(std::isfinite(v));
    CHECK(p.latencySamples() == 0);
}
