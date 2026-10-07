#include "doctest.h"
#include "lv20/lv20.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv20;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
int bandOf(const Processor& p, double hz) { int best = 0; double bd = 1e9; for (int b = 0; b < p.numBands(); ++b) { const double d = std::abs(std::log2(p.bandHz(b) / hz)); if (d < bd) { bd = d; best = b; } } return best; }
std::vector<float> pink(double rms, double seconds, unsigned seed) { auto x = noise(0, seconds, seed); double b0 = 0, b1 = 0, b2 = 0; for (auto& v : x) { const double w = v; b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913; v = static_cast<float>((b0 + b1 + b2 + w * 0.1848) * 0.2); } double s = 0; for (float v : x) s += double(v) * v; const double g = std::pow(10.0, rms / 20.0) / std::sqrt(s / x.size()); for (auto& v : x) v = static_cast<float>(v * g); return x; }
}

TEST_CASE("LV20 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Resolution].labels == std::vector<std::string>{"1/3 oct", "1/6 oct", "1/12 oct"}); CHECK(s[Resolution].def == 0);
    CHECK(s[Speed].labels == std::vector<std::string>{"Slow", "Medium", "Fast"}); CHECK(s[Speed].def == 1);
    CHECK(s[PeakHold].min == 0); CHECK(s[PeakHold].max == 10); CHECK(s[PeakHold].def == 2);
    CHECK(s[Weight].labels == std::vector<std::string>{"Z", "A", "C"}); CHECK(s[Weight].def == 0); CHECK(s[PinkRef].def == 1); CHECK(s[Freeze].def == 0);
    Processor q; CHECK(q.latencySamples() == 0);
    CHECK(weightingDb(1, 1000) == doctest::Approx(0.0).epsilon(0.05)); CHECK(weightingDb(1, 100) == doctest::Approx(-19.1).epsilon(0.02)); CHECK(weightingDb(2, 1000) == doctest::Approx(0.0).epsilon(0.05)); CHECK(weightingDb(0, 77) == 0.0);
}
TEST_CASE("LV20 the sound passes untouched") {
    auto p = make(); const auto x = noise(-20, 1.0, 3), y = run(p, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]);
}
TEST_CASE("LV20 band layout: 1/3, 1/6 and 1/12 octave from 20 Hz to 20 kHz") {
    auto a = make({{Resolution, 0}}); CHECK(a.numBands() == 31); CHECK(a.bandHz(0) >= 19.5); CHECK(a.bandHz(0) < 26); CHECK(a.bandHz(a.numBands() - 1) <= 20500.0);
    auto b = make({{Resolution, 1}}); CHECK(b.numBands() >= 60); auto c = make({{Resolution, 2}}); CHECK(c.numBands() >= 120); CHECK(c.numBands() <= kMaxBands);
    CHECK(a.bandHz(bandOf(a, 1000.0)) == doctest::Approx(1000.0));
}
TEST_CASE("LV20 a sine reads its level in its band and is far down elsewhere") {
    auto p = make({{Speed, 2}}); run(p, sine(-20, 3.0, 1000)); const int b = bandOf(p, 1000.0);
    CHECK(std::abs(p.levelDb(b) - (-20.0)) < 0.7); CHECK(p.levelDb(bandOf(p, 125.0)) < p.levelDb(b) - 50.0); CHECK(p.levelDb(bandOf(p, 8000.0)) < p.levelDb(b) - 50.0);
}
TEST_CASE("LV20 Resolution 1/12: a narrower band holds the same sine") {
    for (int res : {1, 2}) { auto p = make({{Speed, 2}, {Resolution, res}}); run(p, sine(-20, 3.0, 2000)); const int b = bandOf(p, 2000.0); CHECK(std::abs(p.levelDb(b) - (-20.0)) < 1.2); }
}
TEST_CASE("LV20 Weight: A and C change the low bands, not 1 kHz") {
    auto z = make({{Speed, 2}, {Weight, 0}}), a = make({{Speed, 2}, {Weight, 1}}), c = make({{Speed, 2}, {Weight, 2}}); const auto x = sine(-20, 3.0, 100); run(z, x); run(a, x); run(c, x);
    const int b = bandOf(z, 100.0); CHECK(z.levelDb(b) - a.levelDb(b) > 17.0); CHECK(z.levelDb(b) - a.levelDb(b) < 21.0); CHECK(std::abs((z.levelDb(b) - c.levelDb(b)) - 0.3) < 0.8);
    auto a2 = make({{Speed, 2}, {Weight, 1}}), z2 = make({{Speed, 2}, {Weight, 0}}); const auto k = sine(-20, 3.0, 1000); run(a2, k); run(z2, k); CHECK(std::abs(a2.levelDb(bandOf(a2, 1000)) - z2.levelDb(bandOf(z2, 1000))) < 0.3);
}
TEST_CASE("LV20 Speed: Fast follows a level change, Slow lags; Freeze keeps the numbers") {
    auto fast = make({{Speed, 2}}), slow = make({{Speed, 0}}); const auto loud = sine(-10, 2.0, 1000), quiet = sine(-40, 0.5, 1000); run(fast, loud); run(slow, loud); run(fast, quiet); run(slow, quiet);
    CHECK(fast.levelDb(bandOf(fast, 1000)) < slow.levelDb(bandOf(slow, 1000)) - 6.0);
    auto f = make({{Speed, 2}}); run(f, sine(-20, 2.0, 1000)); const double before = f.levelDb(bandOf(f, 1000)); f.setParam(Freeze, 1); run(f, sine(-50, 2.0, 1000)); CHECK(f.levelDb(bandOf(f, 1000)) == before);
}
TEST_CASE("LV20 Peak hold keeps the maximum, then lets it fall") {
    auto p = make({{Speed, 2}, {PeakHold, 1.0}}); run(p, sine(-10, 1.0, 1000)); const int b = bandOf(p, 1000.0); const double top = p.peakDb(b);
    run(p, sine(-50, 0.5, 1000)); CHECK(p.peakDb(b) == doctest::Approx(top).epsilon(0.001)); CHECK(p.levelDb(b) < top - 10.0);
    run(p, sine(-50, 3.0, 1000)); CHECK(p.peakDb(b) < top - 10.0);
    auto o = make({{Speed, 2}, {PeakHold, 0.0}}); run(o, sine(-10, 1.0, 1000)); run(o, sine(-50, 1.0, 1000)); CHECK(std::abs(o.peakDb(b) - o.levelDb(b)) < 0.01);
}
TEST_CASE("LV20 Pink ref: pink noise is flat in 1/3-octave bands; a bass-heavy room leans") {
    auto p = make({{Speed, 0}}); run(p, pink(-20, 12.0, 3));
    double mn = 1e9, mx = -1e9; for (int b = 0; b < p.numBands(); ++b) if (p.bandHz(b) >= 63.0 && p.bandHz(b) <= 12000.0) { mn = std::min(mn, p.deviationDb(b)); mx = std::max(mx, p.deviationDb(b)); }
    CHECK(mx - mn < 4.0);
    auto x = pink(-20, 12.0, 3); const auto bass = sine(-12, 12.0, 80); for (size_t i = 0; i < x.size(); ++i) x[i] += bass[i];
    auto q = make({{Speed, 0}}); run(q, x); CHECK(q.deviationDb(bandOf(q, 80.0)) > 5.0);
}
TEST_CASE("LV20 mono, odd blocks, silence, before prepare") {
    auto p = make(); std::vector<float> l = noise(-20, 1.5, 3); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; p.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    auto q = make(); run(q, std::vector<float>(48000, 0.0f)); CHECK(q.levelDb(10) <= -199.0);
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f);
}
