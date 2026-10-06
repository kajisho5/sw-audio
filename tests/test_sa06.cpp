#include "doctest.h"
#include "sa06/sa06.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::sa06;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// settings for band 2 (200 Hz .. 3 kHz) only
Set band2(int type, double drive, int shape = 0, double bias = 0, double dyn = 0) { return {{band(1, BType), static_cast<double>(type)}, {band(1, BDrive), drive}, {band(1, BShape), static_cast<double>(shape)}, {band(1, BBias), bias}, {band(1, BDyn), dyn}}; }
double h(Set s, int k, double db = -12, double f = 1000) { auto p = make(s); return harmDb(run(p, sine(db, 2, f)), f, k); }
}

TEST_CASE("SA06 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* kn[] = {"type", "drive", "shape", "bias", "dynamics", "mix"};
    for (int n = 0; n < 3; ++n) for (int k = 0; k < 6; ++k) CHECK(std::string(s[static_cast<size_t>(band(n, k))].id) == "sa06.b" + std::to_string(n + 1) + "." + kn[k]);
    CHECK(std::string(s[Tone].id) == "sa06.tone"); CHECK(std::string(s[Output].id) == "sa06.output");
    for (int n = 0; n < 3; ++n) {
        CHECK(s[static_cast<size_t>(band(n, BType))].labels == std::vector<std::string>{"Tape", "Tube", "Diode", "Fold", "Fuzz"}); CHECK(s[static_cast<size_t>(band(n, BType))].def == 1);
        CHECK(s[static_cast<size_t>(band(n, BDrive))].min == 0); CHECK(s[static_cast<size_t>(band(n, BDrive))].max == 24); CHECK(s[static_cast<size_t>(band(n, BDrive))].def == 0);
        CHECK(s[static_cast<size_t>(band(n, BShape))].labels == std::vector<std::string>{"Soft", "Medium", "Hard"}); CHECK(s[static_cast<size_t>(band(n, BShape))].def == 0);
        CHECK(s[static_cast<size_t>(band(n, BBias))].min == -1); CHECK(s[static_cast<size_t>(band(n, BBias))].max == 1); CHECK(s[static_cast<size_t>(band(n, BBias))].def == 0);
        CHECK(s[static_cast<size_t>(band(n, BDyn))].min == -5); CHECK(s[static_cast<size_t>(band(n, BDyn))].max == 5);
        CHECK(s[static_cast<size_t>(band(n, BMix))].def == 100);
    }
    CHECK(s[Tone].min == -6); CHECK(s[Tone].max == 6); CHECK(s[Output].min == -24); CHECK(s[Output].max == 24);
}
TEST_CASE("SA06 with no drive the three bands add back flat and small signals pass at unity") {
    for (double f : {50.0, 200.0, 800.0, 3000.0, 8000.0, 14000.0}) { auto p = make(); NEAR(rmsDb(run(p, sine(-40, 1, f))) - -40.0, 0.0, 0.2); }
}
TEST_CASE("SA06 the five types: Tape odd, Tube even, Diode both, Fold and Fuzz rich") {
    CHECK(h(band2(0, 12), 3) > h(band2(0, 12), 2) + 20.0);
    CHECK(h(band2(1, 12), 2) > h(band2(1, 12), 3) - 3.0);
    CHECK(h(band2(2, 12), 2) > -35.0); CHECK(h(band2(2, 12), 3) > -35.0);
    CHECK(h(band2(3, 18), 3) > -30.0); CHECK(std::max(h(band2(3, 18), 2), h(band2(3, 18), 5)) > -40.0);   // a folded sine is rich in both
    CHECK(h(band2(4, 18), 3) > -25.0); CHECK(h(band2(4, 18), 2) > -30.0);
}
TEST_CASE("SA06 Shape: Hard distorts more than Soft; Bias brings in the even harmonics") {
    CHECK(h(band2(0, 12, 2), 5) > h(band2(0, 12, 0), 5) + 6.0);   // a harder knee: more of the higher harmonics
    CHECK(h(band2(1, 12, 2), 3) > h(band2(1, 12, 0), 3) + 6.0);
    CHECK(h(band2(0, 12, 0, 0), 2) < -80.0); CHECK(h(band2(0, 12, 0, 1), 2) > -40.0);
}
TEST_CASE("SA06 each band works on its own band; Mix 0 leaves the band as it was") {
    Set b3 = {{band(2, BType), 0}, {band(2, BDrive), 18}};   // the high band (3 kHz up)
    auto low = make(b3); NEAR(rmsDb(run(low, sine(-12, 2, 100))) - -12.0, 0.0, 0.2);
    CHECK(std::max(h(b3, 3, -12, 8000), h(b3, 2, -12, 8000)) > -40.0);
    Set dry = band2(4, 24); dry.push_back({band(1, BMix), 0}); auto p = make(dry); NEAR(rmsDb(run(p, sine(-12, 2, 1000))) - -12.0, 0.0, 0.2);
    CHECK(h(dry, 3) < -60.0);
}
TEST_CASE("SA06 Dynamics (EVO): + distorts the hard hits more, - distorts the soft notes more") {
    auto growth = [](double dyn) { return h(band2(0, 12, 0, 0, dyn), 3, -6) - h(band2(0, 12, 0, 0, dyn), 3, -26); };   // dB of 3rd added when 20 dB louder
    CHECK(growth(5) > growth(0) + 3.0); CHECK(growth(-5) < growth(0) - 3.0);
}
TEST_CASE("SA06 Tone tilts the sum around 1 kHz") {
    auto g = [](double tone, double f) { auto p = make({{Tone, tone}}); return rmsDb(run(p, sine(-30, 2, f))) - -30.0; };
    CHECK(g(6, 8000) - g(6, 100) > 7.0); CHECK(g(-6, 100) - g(-6, 8000) > 7.0); NEAR(g(6, 1000), 0.0, 0.8);
}
TEST_CASE("SA06 silence stays silent, extreme input finite, latency 0") {
    Set s; for (int n = 0; n < 3; ++n) { s.push_back({band(n, BType), 4}); s.push_back({band(n, BDrive), 24}); s.push_back({band(n, BBias), 1}); }
    auto p = make(s);
    for (float v : run(p, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f);
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : run(p, x)) REQUIRE(std::isfinite(v));
    CHECK(p.latencySamples() == 0);
}
