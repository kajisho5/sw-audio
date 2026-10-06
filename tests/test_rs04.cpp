#include "doctest.h"
#include "rs04/rs04.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::rs04;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// a clean "music": three tones and a little noise (-50 dBFS)
std::vector<float> clean(double seconds, unsigned seed = 1) { auto a = sine(-14, seconds, 220), b = sine(-20, seconds, 880), c = sine(-26, seconds, 2500), n = noise(-50, seconds, seed); for (size_t i = 0; i < a.size(); ++i) a[i] += b[i] + c[i] + n[i]; return a; }
// error energy (dB) of y against x delayed by the latency
double errDb(const std::vector<float>& y, const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) { const double d = double(y[i + kLatency]) - x[i]; s += d * d; } return 10 * std::log10(s / static_cast<double>(b - a) + 1e-30); }
// add clicks of `w` samples (random, sigma `amp`) every 4800 samples after the first second (a pattern such as +-+- would be predictable by the AR model: only its edges would show)
std::vector<float> withClicks(const std::vector<float>& x, int w, float amp, int every = 4800) { auto y = x; Gauss g(5); for (size_t s = 48000 + 311; s + 400 < y.size(); s += static_cast<size_t>(every)) for (int k = 0; k < w; ++k) y[s + static_cast<size_t>(k)] += amp * static_cast<float>(g.gauss()); return y; }
}

TEST_CASE("RS04 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"rs04.target", "rs04.sens", "rs04.width", "rs04.crackle", "rs04.guard"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Target].labels == std::vector<std::string>{"Click", "Crackle", "Both"}); CHECK(s[Target].def == 2);
    CHECK(s[Sensitivity].labels == std::vector<std::string>{"Low", "Mid", "High"}); CHECK(s[Sensitivity].def == 1);
    CHECK(s[ClickWidth].min == 0.1); CHECK(s[ClickWidth].max == 5); CHECK(s[ClickWidth].def == 1); CHECK(s[ClickWidth].curve == Curve::Log);
    CHECK(s[Crackle].min == 0); CHECK(s[Crackle].max == 100); CHECK(s[Crackle].def == 40);
    CHECK(s[LowGuard].labels == std::vector<std::string>{"Off", "On"}); CHECK(s[LowGuard].def == 1);
}
TEST_CASE("RS04 reports 512 samples; silence is silence; a clean signal passes untouched") {
    Processor q; CHECK(q.latencySamples() == 512);
    { auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f); }
    const auto x = clean(5.0); auto p = make({{Target, ClickOnly}}); const auto y = run(p, x);
    for (size_t i = 24000; i + kLatency < y.size(); i += 53) NEAR(y[i + kLatency], x[i], 1e-6);
    CHECK(p.clicksRepaired() == 0);
}
TEST_CASE("RS04 removes clicks: the error against the clean signal drops by more than 20 dB") {
    const auto x = clean(8.0);
    for (int w : {1, 3, 12}) {
        const auto d = withClicks(x, w, 0.5f); auto p = make({{Target, ClickOnly}}); const auto y = run(p, d);
        const double before = errDb(d, x, 48000, 7 * 48000 - 1000) , after = errDb(y, x, 48000, 7 * 48000 - 1000);
        // `d` is not delayed: compare it against x directly for the "before" number
        double s = 0; for (size_t i = 48000; i < 7 * 48000 - 1000; ++i) { const double e = double(d[i]) - x[i]; s += e * e; }
        const double b0 = 10 * std::log10(s / (6 * 48000 - 1000.0));
        (void)before;
        CHECK(after < b0 - 20.0);
        CHECK(p.clicksRepaired() >= 10);
    }
}
TEST_CASE("RS04 Click width limits the length that is repaired") {
    const auto x = clean(8.0); const auto d = withClicks(x, 48, 0.4f);   // 1 ms of random noise at 0.4
    auto err = [&](double wMs) { auto p = make({{Target, ClickOnly}, {ClickWidth, wMs}}); const auto y = run(p, d); return errDb(y, x, 48000, 7 * 48000 - 1000); };
    CHECK(err(1.0) < err(0.3) - 8.0);
}
TEST_CASE("RS04 Sensitivity: a weak click is caught by High and not by Low") {
    const auto x = clean(8.0); const auto d = withClicks(x, 1, 0.006f);   // a click of about 4-5 sigma of the excitation
    auto cnt = [&](double sens) { auto p = make({{Target, ClickOnly}, {Sensitivity, sens}}); run(p, d); return p.clicksRepaired(); };
    CHECK(cnt(2) > cnt(0) + 5);
}
TEST_CASE("RS04 Crackle: tiny short clicks are repaired by the Crackle amount; Target Click mostly leaves them") {
    auto x = clean(8.0, 1); { auto n1 = noise(-50, 8.0, 1), n2 = noise(-60, 8.0, 1); for (size_t i = 0; i < x.size(); ++i) x[i] += n2[i] - n1[i]; }   // the same music with the noise at -60 dBFS (sigma 0.001)
    auto d = x; std::vector<size_t> at; for (size_t s = 48000 + 77; s + 400 < d.size(); s += 997) { d[s] += (s & 1) ? 0.007f : -0.007f; at.push_back(s); }   // about 4.2 sigma (of the measured excitation scale 1.66e-3): above the Crackle threshold (3.5), mostly under the Click one (5)
    // what is left of the spike at the spike positions (1 = nothing repaired, 0 = fully repaired)
    auto left = [&](Set set) { auto p = make(set); const auto y = run(p, d); double s = 0; for (size_t i : at) { const double e = double(y[i + kLatency]) - x[i]; s += e * e; } return s / (static_cast<double>(at.size()) * 0.007 * 0.007); };
    const double click = left({{Target, ClickOnly}}), zero = left({{Target, CrackleOnly}, {Crackle, 0}}), half = left({{Target, CrackleOnly}, {Crackle, 50}}), full = left({{Target, CrackleOnly}, {Crackle, 100}});
    CHECK(click > 0.6); NEAR(zero, 1.0, 0.05); CHECK(half < 0.6); CHECK(half > 0.1); CHECK(full < 0.35); CHECK(full < half);
}
TEST_CASE("RS04 Low guard: a kick-like burst is not taken for a click") {
    auto x = clean(8.0); auto d = x; for (size_t i = 0; i < 24000; ++i) d[3 * 48000 + i] += static_cast<float>(0.6 * std::sin(2 * kPi * 55.0 * i / kFs) * std::exp(-static_cast<double>(i) / 6000.0));
    auto p = make({{Target, Both}, {LowGuard, 1}}); const auto y = run(p, d);
    double s = 0, e = 0; for (size_t i = 3 * 48000; i < 3 * 48000 + 24000; ++i) { const double v = double(d[i]) - y[i + kLatency]; s += v * v; e += double(d[i]) * d[i]; }
    CHECK(10 * std::log10(s / e) < -40.0);
}
TEST_CASE("RS04 loud input stays finite; stereo channels are separate") {
    auto p = make({{Sensitivity, 2}}); auto x = noise(6, 2.0, 9); for (auto& v : x) v *= 8.0f; for (float v : run(p, x)) CHECK(std::isfinite(v));
    auto q = make(); const auto a = clean(2.0, 1); const auto b = clean(2.0, 2); const auto r = run2(q, a, b);
    for (size_t i = 24000; i + kLatency < a.size(); i += 97) { NEAR(r.first[i + kLatency], a[i], 1e-5); NEAR(r.second[i + kLatency], b[i], 1e-5); }
}
