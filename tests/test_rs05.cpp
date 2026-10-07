#include "doctest.h"
#include "rs05/rs05.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::rs05;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// a voiced-like signal: 220 Hz with harmonics, peak about `peak`; hard-clipped at `ceil`
std::vector<float> tonal(double peak, double seconds) { auto a = sine(-20, seconds, 220), b = sine(-26, seconds, 440), c = sine(-30, seconds, 660), d = sine(-36, seconds, 1100); double mx = 0; std::vector<float> y(a.size()); for (size_t i = 0; i < a.size(); ++i) { y[i] = a[i] + b[i] + c[i] + d[i]; mx = std::max(mx, double(std::abs(y[i]))); } for (auto& v : y) v = static_cast<float>(v * peak / mx); return y; }
std::vector<float> clip(const std::vector<float>& x, double ceil) { auto y = x; for (auto& v : y) v = static_cast<float>(std::clamp(double(v), -ceil, ceil)); return y; }
// error (dB) of y against x delayed by the latency, with y scaled by 1/gain
double errDb(const std::vector<float>& y, const std::vector<float>& x, double gain, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) { const double d = double(y[i + kLatency]) / gain - x[i]; s += d * d; } return 10 * std::log10(s / static_cast<double>(b - a) + 1e-30); }
double errDbRaw(const std::vector<float>& c, const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) { const double d = double(c[i]) - x[i]; s += d * d; } return 10 * std::log10(s / static_cast<double>(b - a) + 1e-30); }
}

TEST_CASE("RS05 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"rs05.thresh", "rs05.quality", "rs05.makeup", "rs05.smooth", "rs05.evo.on"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Threshold].min == -3); CHECK(s[Threshold].max == 0); CHECK(s[Threshold].def == -0.5);
    CHECK(s[Quality].labels == std::vector<std::string>{"Low", "Mid", "High"}); CHECK(s[Quality].def == 2);
    CHECK(s[Makeup].min == -12); CHECK(s[Makeup].max == 0); CHECK(s[Makeup].def == -3);
    CHECK(s[Smooth].labels == std::vector<std::string>{"Low", "Mid", "High"}); CHECK(s[Smooth].def == 1);
    CHECK(s[Detect].labels == std::vector<std::string>{"Off", "On"}); CHECK(s[Detect].def == 1);
}
TEST_CASE("RS05 reports 1024 samples; silence is silence; a signal that does not clip passes (with Makeup)") {
    Processor q; CHECK(q.latencySamples() == 1024);
    { auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f); }
    const auto x = tonal(0.5, 3.0); auto p = make({{Makeup, 0}}); const auto y = run(p, x);
    for (size_t i = 24000; i + kLatency < y.size(); i += 53) NEAR(y[i + kLatency], x[i], 1e-6);
    CHECK(p.runsRestored() == 0);
}
TEST_CASE("RS05 restores clipped peaks: the error against the original drops") {
    const auto orig = tonal(1.5, 4.0); const auto clipped = clip(orig, 0.95);   // above the default Threshold (-0.5 dB = 0.944)
    auto p = make({{Makeup, 0}, {Detect, 0}}); const auto y = run(p, clipped);
    const double before = errDbRaw(clipped, orig, 24000, 3 * 48000), after = errDb(y, orig, 1.0, 24000, 3 * 48000);
    CHECK(after < before - 8.0);
    CHECK(p.runsRestored() > 20);
    // the restored peaks go above the ceiling
    float mx = 0; for (size_t i = 24000 + kLatency; i < 3 * 48000; ++i) mx = std::max(mx, std::abs(y[i])); CHECK(mx > 1.05f);
    // and never below it: wherever the input sits at the ceiling the output is at or above it
    for (size_t i = 24000; i < 3 * 48000; ++i) if (std::abs(clipped[i]) >= 0.9499f) CHECK(std::abs(y[i + kLatency]) >= 0.94f);
}
TEST_CASE("RS05 Makeup is a gain on the output") {
    const auto clipped = clip(tonal(1.5, 4.0), 0.95);
    auto a = make({{Makeup, 0}, {Detect, 0}}), b = make({{Makeup, -6}, {Detect, 0}}); const auto ya = run(a, clipped), yb = run(b, clipped);
    for (size_t i = 100000; i < 100000 + 2000; i += 13) NEAR(yb[i], ya[i] * 0.5011872f, 1e-4);
}
TEST_CASE("RS05 Detect reads the ceiling from the samples") {
    const auto orig = tonal(1.5, 4.0); const auto clipped = clip(orig, 0.6);   // far from the Threshold (0.944)
    auto off = make({{Makeup, 0}, {Detect, 0}}), on = make({{Makeup, 0}, {Detect, 1}});
    const auto yo = run(off, clipped), yn = run(on, clipped);
    CHECK(off.runsRestored() == 0); CHECK(on.runsRestored() > 20);
    CHECK(errDb(yn, orig, 1.0, 48000, 3 * 48000) < errDbRaw(clipped, orig, 48000, 3 * 48000) - 5.0);
    NEAR(on.ceilingUsed(), 0.6 * 0.995, 0.01);
    for (size_t i = 48000; i + kLatency < yo.size(); i += 53) NEAR(yo[i + kLatency], clipped[i], 1e-6);   // Off: nothing is touched
}
TEST_CASE("RS05 Quality and Smooth") {
    const auto orig = tonal(1.5, 4.0), clipped = clip(orig, 0.95);
    auto err = [&](Set set) { set.push_back({Makeup, 0}); set.push_back({Detect, 0}); auto p = make(set); const auto y = run(p, clipped); return errDb(y, orig, 1.0, 24000, 3 * 48000); };
    const double lo = err({{Quality, 0}}), hi = err({{Quality, 2}});
    CHECK(hi <= lo + 1.0);
    auto peak = [&](double smooth) { auto p = make({{Makeup, 0}, {Detect, 0}, {Smooth, smooth}}); const auto y = run(p, clip(tonal(3.0, 4.0), 0.95)); float mx = 0; for (size_t i = 24000; i < y.size(); ++i) mx = std::max(mx, std::abs(y[i])); return mx; };
    CHECK(peak(0) > peak(2));   // Low smooth lets the restored peaks go higher
    CHECK(peak(2) <= 0.944 * std::pow(10.0, 3.0 / 20.0) * 1.001);
}
TEST_CASE("RS05 loud input stays finite; stereo channels are separate") {
    auto p = make({{Quality, 2}}); auto x = noise(6, 2.0, 9); for (auto& v : x) v *= 8.0f; for (float v : run(p, x)) CHECK(std::isfinite(v));
    auto q = make({{Makeup, 0}}); const auto a = tonal(0.4, 2.0), b = tonal(0.3, 2.0); const auto r = run2(q, a, b);
    for (size_t i = 24000; i + kLatency < a.size(); i += 97) { NEAR(r.first[i + kLatency], a[i], 1e-5); NEAR(r.second[i + kLatency], b[i], 1e-5); }
}
