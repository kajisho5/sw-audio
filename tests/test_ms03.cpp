#include "doctest.h"
#include "ms03/ms03.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::ms03;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> plus(std::vector<float> a, const std::vector<float>& b) { for (size_t i = 0; i < a.size(); ++i) a[i] += b[i]; return a; }
}

TEST_CASE("MS03 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* kn[] = {"gain", "ceiling", "release"};
    for (int n = 0; n < 4; ++n) for (int k = 0; k < 3; ++k) CHECK(std::string(s[static_cast<size_t>(band(n, k))].id) == "ms03.b" + std::to_string(n + 1) + "." + kn[k]);
    for (int n = 0; n < 4; ++n) {
        const auto& g = s[static_cast<size_t>(band(n, BGain))]; CHECK(g.min == 0); CHECK(g.max == 12); CHECK(g.def == 0);
        const auto& c = s[static_cast<size_t>(band(n, BCeiling))]; CHECK(c.min == -12); CHECK(c.max == 0); CHECK(c.def == 0);
        const auto& r = s[static_cast<size_t>(band(n, BRelease))]; CHECK(r.min == 1); CHECK(r.max == 1000); CHECK(r.def == 60); CHECK(r.skew == 3);
    }
    CHECK(std::string(s[X1].id) == "ms03.x1.freq"); CHECK(s[X1].def == 120); CHECK(s[X2].def == 1000); CHECK(s[X3].def == 6000);
    CHECK(std::string(s[OutCeiling].id) == "ms03.outceiling"); CHECK(s[OutCeiling].def == -1); CHECK(s[OutCeiling].min == -12);
    CHECK(s[Char].labels == std::vector<std::string>{"Clean", "Punch", "Dense"}); CHECK(s[Char].def == 1);
    CHECK(std::string(s[Link].id) == "ms03.evo.on"); CHECK(s[Link].def == 1);
}
TEST_CASE("MS03 latency is the band look-ahead + link stage + final true-peak stage (184 @48 kHz)") {
    Processor p; CHECK(p.latencySamples() == 96 + 48 + 40);
}
TEST_CASE("MS03 the four bands add back flat when nothing is limited") {
    for (double f : {40.0, 120.0, 500.0, 1000.0, 3000.0, 6000.0, 12000.0}) { auto p = make(); NEAR(rmsDb(run(p, sine(-30, 1, f))), -30.0, 0.2); }
}
TEST_CASE("MS03 band Gain and band Ceiling act on their own band") {
    auto lvl = [](Set s, double f, double db = -20) { auto p = make(s); return rmsDb(run(p, sine(db, 2, f))); };
    NEAR(lvl({{band(0, BGain), 6}}, 50) - lvl({}, 50), 6.0, 0.7);
    NEAR(lvl({{band(0, BGain), 6}}, 3000) - lvl({}, 3000), 0.0, 0.3);
    // band 2 (120 Hz .. 1 kHz) ceiling -12 dB: a loud 500 Hz tone comes out at or below -12 dB peak, a 3 kHz tone is untouched
    auto p = make({{band(1, BCeiling), -12}}); const auto y = run(p, sine(-3, 2, 500));
    CHECK(peakDb(y, 48000, 96000) <= -12.0 + 2.5);        // band 3 still carries a -24.6 dB leak of 500 Hz (LR4 edge), which adds to the limited band 2
    CHECK(peakDb(y, 48000, 96000) < peakDb(sine(-3, 2, 500), 48000, 96000) - 8.0);
    NEAR(lvl({{band(1, BCeiling), -12}}, 3000, -3) , lvl({}, 3000, -3), 0.3);
}
TEST_CASE("MS03 Out ceiling is always kept (sample peak)") {
    Set s; for (int n = 0; n < 4; ++n) s.push_back({band(n, BGain), 12});
    for (int link : {0, 1}) {
        Set t = s; t.push_back({Link, static_cast<double>(link)});
        auto p = make(t);
        const auto y = run(p, noise(-8, 3, 4));
        CHECK(peakDb(y, 48000, y.size()) <= -1.0 + 0.1);
    }
}
TEST_CASE("MS03 Link bands: the bass that overloads the sum takes the reduction, the mid tone is left steadier") {
    // 80 Hz bursts (band 1) + a steady 2 kHz tone (band 3); every band ceiling at 0 dB, so only the sum overloads the -1 dBTP ceiling
    std::vector<float> bass(static_cast<size_t>(4 * kFs), 0.0f);
    for (size_t i = 0; i < bass.size(); ++i) if ((i / 12000) % 2 == 0) bass[i] = static_cast<float>(0.85 * std::sin(2 * kPi * 80 * i / kFs));
    const auto in = plus(bass, [] { auto t = sine(-9, 4, 2000); return t; }());
    auto toneDuringBass = [&](int link) {
        auto p = make({{Link, static_cast<double>(link)}});
        const auto y = run(p, in);
        return binDb(y, 2000, 48000 + 4800, 48000 + 5760);   // 100 ms into a bass burst (bursts at 48000..60000)
    };
    const double quiet = [&] { auto p = make(); const auto y = run(p, in); return binDb(y, 2000, 48000 + 9600, 48000 + 10560); }();   // between bursts
    const double off = toneDuringBass(0), on = toneDuringBass(1);
    CHECK(on > off + 0.3);               // the tone ducks less when the bass takes its share
    CHECK(on < quiet);                   // the sum still has to fit under the ceiling
}
TEST_CASE("MS03 silence stays silent, extreme input finite") {
    { auto z = make(); for (float v : run(z, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f); }
    auto p = make({{band(0, BGain), 12}, {band(3, BGain), 12}});
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : run(p, x)) REQUIRE(std::isfinite(v));
}
