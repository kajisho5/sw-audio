#include "doctest.h"
#include "sa03/sa03.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::sa03;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
double h(Set s, int k, double db = -12, double f = 1000) { auto p = make(s); return harmDb(run(p, sine(db, 2, f)), f, k); }
}

TEST_CASE("SA03 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"sa03.drive", "sa03.bias", "sa03.tone", "sa03.tube", "sa03.mix", "sa03.output", "sa03.evo.on"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Drive].max == 10); CHECK(s[Drive].def == 3);
    CHECK(s[Bias].def == 0.5); CHECK(std::string(s[Bias].minLabel) == "Cold"); CHECK(std::string(s[Bias].maxLabel) == "Hot");
    CHECK(s[Tone].min == -6); CHECK(s[Tone].max == 6); CHECK(s[Tone].def == 0);
    CHECK(s[Tube].labels == std::vector<std::string>{"12AX7", "12AT7", "EL34"}); CHECK(s[Tube].def == 0);
    CHECK(s[Mix].def == 100); CHECK(s[Output].min == -10); CHECK(s[Output].max == 10);
    CHECK(s[Evo].labels == std::vector<std::string>{"Off", "On"});
}
TEST_CASE("SA03 Drive adds harmonics, small signals pass at unity") {
    CHECK(h({{Drive, 8}, {Evo, 0}}, 2) > h({{Drive, 2}, {Evo, 0}}, 2) + 6.0);
    CHECK(h({{Drive, 8}, {Evo, 0}}, 3) > h({{Drive, 2}, {Evo, 0}}, 3) + 6.0);
    auto p = make({{Drive, 10}}); NEAR(rmsDb(run(p, sine(-50, 2, 1000))) - -50.0, 0.0, 0.15);
}
TEST_CASE("SA03 Tube: 12AX7 leans even, EL34 leans odd") {
    const Set base = {{Drive, 6}, {Evo, 0}};
    auto lean = [&](double tube) { Set s = base; s.push_back({Tube, tube}); return h(s, 3) - h(s, 2); };   // dB of 3rd over 2nd
    CHECK(lean(0) < 0.0); CHECK(lean(2) > lean(0) + 3.0); CHECK(lean(1) > lean(0)); CHECK(lean(1) < lean(2));
}
TEST_CASE("SA03 Bias: Cold is symmetric (odd only), Hot is asymmetric (even harmonics)") {
    const Set cold = {{Drive, 6}, {Bias, 0}, {Evo, 0}}, hot = {{Drive, 6}, {Bias, 1}, {Evo, 0}};
    CHECK(h(cold, 2) < -80.0); CHECK(h(hot, 2) > h(cold, 2) + 40.0); CHECK(h(hot, 2) > -35.0);
}
TEST_CASE("SA03 moving bias: louder input -> more asymmetric (even harmonics grow faster than the level)") {
    auto growth = [](int evo) { const Set s = {{Drive, 5}, {Evo, static_cast<double>(evo)}}; return h(s, 2, -6) - h(s, 2, -26); };   // dB more 2nd harmonic (relative) when 20 dB louder
    CHECK(growth(1) > growth(0) + 3.0);
    // 50 ms return: a loud burst moves the bias, the quiet part after it settles back
    auto p = make({{Drive, 5}});
    std::vector<float> x(48000 * 2, 0.0f); const auto loud = sine(-6, 0.3, 1000), quiet = sine(-30, 0.8, 1000);
    std::copy(loud.begin(), loud.end(), x.begin()); std::copy(quiet.begin(), quiet.end(), x.begin() + 24000);
    run(p, x); CHECK(p.biasShift() < 0.01);
}
TEST_CASE("SA03 Tone tilts around 1 kHz") {
    auto g = [](double tone, double f) { auto p = make({{Drive, 0}, {Tone, tone}}); return rmsDb(run(p, sine(-30, 2, f))) - -30.0; };
    CHECK(g(6, 8000) - g(6, 100) > 7.0); CHECK(g(-6, 100) - g(-6, 8000) > 7.0); NEAR(g(6, 1000), 0.0, 0.7);
}
TEST_CASE("SA03 silence stays silent, extreme input finite, latency 0") {
    auto p = make({{Drive, 10}, {Bias, 1}, {Tone, 6}});
    for (float v : run(p, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f);
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : run(p, x)) REQUIRE(std::isfinite(v));
    CHECK(p.latencySamples() == 0);
}
