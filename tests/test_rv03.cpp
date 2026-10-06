#include "doctest.h"
#include "rv03/rv03.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::rv03;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> impulse(Processor& p, double sec) { std::vector<float> x(static_cast<size_t>(sec * kFs), 0.0f); x[0] = 1.0f; return run(p, x); }
double crossing(const std::vector<float>& h, double db) {
    double tot = 0; for (float v : h) tot += double(v) * v; double e = tot;
    for (size_t i = 0; i < h.size(); ++i) { if (10 * std::log10(e / tot + 1e-30) <= -db) return static_cast<double>(i) / kFs; e -= double(h[i]) * h[i]; }
    return static_cast<double>(h.size()) / kFs;
}
double energy(const std::vector<float>& h, double a, double b) { double e = 0; for (size_t i = static_cast<size_t>(a * kFs); i < std::min(h.size(), static_cast<size_t>(b * kFs)); ++i) e += double(h[i]) * h[i]; return e; }
double band(const std::vector<float>& y, double f0, double f1, size_t a, size_t b) { double s = 0; int n = 0; for (double f = f0; f <= f1; f *= 1.1) { s += std::pow(10.0, binDb(y, f, a, b) / 10.0); ++n; } return 10 * std::log10(s / n + 1e-30); }
std::vector<float> burst(double rmsDbfs, double sec, double total, unsigned seed = 1) { auto x = noise(rmsDbfs, sec, seed); x.resize(static_cast<size_t>(total * kFs), 0.0f); return x; }
double ripple(const std::vector<float>& h) { std::vector<double> d; for (double f = 300; f < 4000; f += 11) d.push_back(binDb(h, f, 0, h.size())); double m = 0; for (double v : d) m += v; m /= static_cast<double>(d.size()); double s = 0; for (double v : d) s += (v - m) * (v - m); return std::sqrt(s / static_cast<double>(d.size())); }
double crest(const std::vector<float>& h, double a, double b) { double mx = 0, e = 0; size_t n = 0; for (size_t i = static_cast<size_t>(a * kFs); i < std::min(h.size(), static_cast<size_t>(b * kFs)); ++i) { mx = std::max(mx, double(std::abs(h[i]))); e += double(h[i]) * h[i]; ++n; } return mx / std::sqrt(e / n); }
}

TEST_CASE("RV03 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"rv03.springs", "rv03.dwell", "rv03.tone", "rv03.tension", "rv03.drip", "rv03.mix"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Springs].steps == std::vector<double>{1, 2, 3}); CHECK(s[Springs].def == 2);
    for (int i : {Dwell, Tension, Drip}) { CHECK(s[static_cast<size_t>(i)].min == 0); CHECK(s[static_cast<size_t>(i)].max == 10); CHECK(s[static_cast<size_t>(i)].def == 5); }
    CHECK(s[Tone].def == 50); CHECK(std::string(s[Tone].minLabel) == "Dark"); CHECK(std::string(s[Tone].maxLabel) == "Bright"); CHECK(s[Mix].def == 30);
}
TEST_CASE("RV03 no delay is reported; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("RV03 the chirp: lows are delayed more than highs, and Tension moves it") {
    CHECK(dispersionDelay(150, kFs, 5) > 3.0 * dispersionDelay(5000, kFs, 5));
    CHECK(dispersionDelay(150, kFs, 5) > 100.0);
    CHECK(dispersionDelay(150, kFs, 10) != doctest::Approx(dispersionDelay(150, kFs, 0)));
    // a tighter spring is stiffer: shorter delay for the same low frequency
    CHECK(dispersionDelay(300, kFs, 10) < dispersionDelay(300, kFs, 0));
}
TEST_CASE("RV03 the springs ring for a couple of seconds") {
    auto p = make(); const auto h = impulse(p, 6.0);
    const double t = crossing(h, 30.0); CHECK(t > 0.6); CHECK(t < 2.5);
}
TEST_CASE("RV03 more springs, a denser sound") {
    auto one = make({{Springs, 1}}); auto three = make({{Springs, 3}});
    const auto h1 = impulse(one, 2.0), h3 = impulse(three, 2.0);
    // the spectrum is less combed with more springs (std of the level in dB over 300 Hz .. 4 kHz)
    CHECK(ripple(h3) < ripple(h1) - 2.0);
    // one spring is mono
    auto m = make({{Springs, 1}}); std::vector<float> x(24000, 0.0f); x[0] = 1; const auto o = run2(m, x, x); for (size_t i = 0; i < x.size(); ++i) CHECK(o.first[i] == o.second[i]);
}
TEST_CASE("RV03 Dwell drives the springs, with some saturation") {
    const auto x = burst(-20, 0.5, 1.5);
    auto d0 = make({{Dwell, 0}}); auto d5 = make({{Dwell, 5}}); auto d10 = make({{Dwell, 10}});
    const double l0 = rmsDb(run(d0, x), 0, x.size()), l5 = rmsDb(run(d5, x), 0, x.size()), l10 = rmsDb(run(d10, x), 0, x.size());
    CHECK(l5 > l0 + 8.0); CHECK(l10 > l5 + 3.0); CHECK(l10 < l5 + 17.0);
}
TEST_CASE("RV03 Tone: Dark loses the highs") {
    auto dark = make({{Tone, 0}}); auto bright = make({{Tone, 100}});
    const auto x = burst(-20, 0.5, 2.0), yd = run(dark, x), yb = run(bright, x);
    const size_t a = static_cast<size_t>(0.8 * kFs), b = static_cast<size_t>(1.8 * kFs);
    CHECK(band(yb, 3000, 6000, a, b) - band(yd, 3000, 6000, a, b) > band(yb, 200, 400, a, b) - band(yd, 200, 400, a, b) + 6.0);
}
TEST_CASE("RV03 Drip: strong on attacks, nothing on a held note") {
    auto lo = make({{Drip, 0}}); auto hi = make({{Drip, 10}});
    const auto hl = impulse(lo, 1.0), hh = impulse(hi, 1.0);
    CHECK(energy(hh, 0.0, 0.2) > energy(hl, 0.0, 0.2) * 3.0);
    const auto st = sine(-20, 2.5, 220.0); auto a = make({{Drip, 0}}); auto b = make({{Drip, 10}});
    NEAR(rmsDb(run(b, st), static_cast<size_t>(1.5 * kFs), static_cast<size_t>(2.5 * kFs)) - rmsDb(run(a, st), static_cast<size_t>(1.5 * kFs), static_cast<size_t>(2.5 * kFs)), 0.0, 1.0);
}
TEST_CASE("RV03 the level is sensible and the extremes stay finite") {
    auto p = make(); const auto h = impulse(p, 4.0); double e = 0; for (float v : h) e += double(v) * v; CHECK(e > 0.1); CHECK(e < 5.0);
    for (double dw : {0.0, 10.0}) for (double tn : {0.0, 10.0}) { auto q = make({{Springs, 3}, {Dwell, dw}, {Tension, tn}, {Drip, 10}}); for (float v : run(q, burst(-3, 1.0, 3.0))) { CHECK(std::isfinite(v)); CHECK(std::abs(v) < 8.0f); } }
}
