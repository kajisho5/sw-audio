#include "doctest.h"
#include "rs01/rs01.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::rs01;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> mix(const std::vector<float>& a, const std::vector<float>& b) { std::vector<float> y(a.size()); for (size_t i = 0; i < a.size(); ++i) y[i] = a[i] + b[i]; return y; }
constexpr size_t kLat = 2048;
}

TEST_CASE("RS01 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"rs01.profile", "rs01.evo.on", "rs01.reduction", "rs01.thresh", "rs01.smooth", "rs01.low", "rs01.high", "rs01.guard", "rs01.learn"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Profile].labels == std::vector<std::string>{"Voice", "Music", "Field"}); CHECK(s[Profile].def == 0);
    CHECK(s[Adaptive].labels == std::vector<std::string>{"Off", "On"}); CHECK(s[Adaptive].def == 1);
    CHECK(s[Reduction].min == -40); CHECK(s[Reduction].max == 0); CHECK(s[Reduction].def == -12);
    CHECK(s[Threshold].min == -10); CHECK(s[Threshold].max == 20); CHECK(s[Threshold].def == 3);
    CHECK(s[Smoothing].labels == std::vector<std::string>{"Low", "Mid", "High"}); CHECK(s[Smoothing].def == 1);
    for (int i : {LowBand, HighBand}) { CHECK(s[static_cast<size_t>(i)].min == -20); CHECK(s[static_cast<size_t>(i)].max == 20); CHECK(s[static_cast<size_t>(i)].def == 0); }
    CHECK(s[Guard].labels == std::vector<std::string>{"Off", "On"}); CHECK(s[Guard].def == 1);
    CHECK(s[Learn].labels == std::vector<std::string>{"Off", "On"}); CHECK_FALSE(s[Learn].automatable);
}
TEST_CASE("RS01 reports 2048 samples; silence is silence; Reduction 0 passes the signal delayed") {
    Processor q; CHECK(q.latencySamples() == 2048);
    { auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f); }
    auto p = make({{Reduction, 0}}); const auto x = mix(sine(-20, 2.0, 700), noise(-35, 2.0, 3)); const auto y = run(p, x);
    for (size_t i = 6000; i < y.size(); i += 311) NEAR(y[i], x[i - kLat], 2e-4);
}
TEST_CASE("RS01 takes the noise down by Reduction and keeps a signal that comes and goes") {
    // a steady tone is indistinguishable from the noise floor (minimum statistics follow it): the signal here is a tone burst, 0.25 s on and 0.25 s off
    const auto noiseOnly = noise(-40, 6.0, 3); auto tone = sine(-20, 6.0, 1000); for (size_t i = 0; i < tone.size(); ++i) if ((i / 12000) % 2 == 1) tone[i] = 0.0f;
    for (double red : {-12.0, -24.0}) {
        auto p = make({{Reduction, red}}); const auto y = run(p, mix(noiseOnly, tone));
        auto q = make({{Reduction, red}}); const auto n = run(q, noiseOnly);
        const double noiseDrop = rmsDb(n, 5 * 48000, 6 * 48000) - rmsDb(noiseOnly, 5 * 48000 - kLat, 6 * 48000 - kLat);
        NEAR(noiseDrop, red, 3.5);
        const size_t a = 5 * 48000 + 12000 * 0 + 24000 / 2 * 0;   // input [5.0, 5.25) is a burst (5.0 / 0.25 = 20, even)
        NEAR(binDb(y, 1000, a + kLat + 2400, a + kLat + 12000 - 2400), binDb(tone, 1000, a + 2400, a + 12000 - 2400), 1.5);   // the tone keeps its level
    }
}
TEST_CASE("RS01 Adaptive follows a change of the noise; Off keeps the old estimate") {
    auto run2 = [&](double adaptive) {
        auto p = make({{Adaptive, adaptive}, {Reduction, -30}});
        std::vector<float> x = noise(-50, 4.0, 5); const auto loud = noise(-35, 8.0, 6); x.insert(x.end(), loud.begin(), loud.end());
        const auto y = run(p, x); return rmsDb(y, 11 * 48000, 12 * 48000) - rmsDb(x, 11 * 48000 - kLat, 12 * 48000 - kLat);
    };
    const double on = run2(1), off = run2(0);
    CHECK(on < -15.0);      // learned the louder noise again
    CHECK(off > -3.0);      // never learned: nothing is taken (the estimate was frozen before any signal)
}
TEST_CASE("RS01 Learn: a profile from noise alone, then Adaptive Off") {
    auto p = make({{Adaptive, 0}, {Reduction, -24}});
    const auto n1 = noise(-40, 2.0, 5); run(p, std::vector<float>(48000, 0.0f));
    p.setParam(Learn, 1); run(p, n1); p.setParam(Learn, 0);
    const auto tone = sine(-20, 4.0, 1000), nz = noise(-40, 4.0, 7); const auto y = run(p, mix(tone, nz));
    NEAR(binDb(y, 1000, 2 * 48000, 4 * 48000), binDb(tone, 1000, 2 * 48000 - kLat, 4 * 48000 - kLat), 1.0);
    // noise away from the tone: lowered by about 24 dB
    auto band = [&](const std::vector<float>& s, size_t a, size_t b) { double e = 0; for (double f = 3000; f < 9000; f += 100) e += std::pow(10.0, binDb(s, f, a, b) / 10); return 10 * std::log10(e); };
    NEAR(band(y, 2 * 48000, 4 * 48000) - band(nz, 2 * 48000 - kLat, 4 * 48000 - kLat), -24.0, 4.0);
}
TEST_CASE("RS01 Low band / High band deepen or lighten the reduction in their range") {
    const auto nz = noise(-40, 6.0, 3);
    auto drop = [&](Set set, double lo, double hi) { auto p = make(set); const auto y = run(p, nz); double e = 0, e0 = 0; for (double f = lo; f < hi; f += 50) { e += std::pow(10.0, binDb(y, f, 5 * 48000, 6 * 48000) / 10); e0 += std::pow(10.0, binDb(nz, f, 5 * 48000 - kLat, 6 * 48000 - kLat) / 10); } return 10 * std::log10(e / e0); };
    const double base = drop({{Reduction, -12}}, 6000, 12000), baseLo = drop({{Reduction, -12}}, 100, 300);
    CHECK(drop({{Reduction, -12}, {HighBand, 12}}, 6000, 12000) < base - 6.0);
    NEAR(drop({{Reduction, -12}, {HighBand, 12}}, 100, 300), baseLo, 1.5);      // the other end is not touched
    CHECK(drop({{Reduction, -12}, {LowBand, 12}}, 100, 300) < baseLo - 6.0);
    CHECK(drop({{Reduction, -12}, {HighBand, -20}}, 6000, 12000) > -2.0);       // shallower: nearly nothing taken there
}
TEST_CASE("RS01 Threshold: a signal a few dB over the noise is taken as noise") {
    const auto nz = noise(-40, 6.0, 3), weak = sine(-33, 6.0, 1000);   // a tone about 7 dB over the per-bin noise
    auto kept = [&](double thr) { auto p = make({{Threshold, thr}, {Reduction, -30}}); const auto y = run(p, mix(nz, weak)); return binDb(y, 1000, 5 * 48000, 6 * 48000) - binDb(weak, 1000, 5 * 48000 - kLat, 6 * 48000 - kLat); };
    CHECK(kept(-10) > kept(20) + 3.0);
}
TEST_CASE("RS01 Artifact guard smooths the gain; loud input stays finite") {
    const auto nz = noise(-40, 6.0, 3);
    auto flick = [&](double guard) { auto p = make({{Guard, guard}, {Reduction, -30}}); const auto y = run(p, nz); std::vector<double> e; for (size_t i = 5 * 48000; i + 480 < y.size(); i += 480) { double s = 0; for (size_t k = 0; k < 480; ++k) s += double(y[i + k]) * y[i + k]; e.push_back(10 * std::log10(s / 480 + 1e-20)); }
                                  double m = 0; for (double v : e) m += v; m /= static_cast<double>(e.size()); double sd = 0; for (double v : e) sd += (v - m) * (v - m); return std::sqrt(sd / static_cast<double>(e.size())); };
    CHECK(flick(1) < flick(0));
    auto p = make({{Reduction, -40}, {HighBand, 20}, {LowBand, 20}}); auto x = noise(6, 2.0, 9); for (auto& v : x) v *= 8.0f; for (float v : run(p, x)) CHECK(std::isfinite(v));
}
