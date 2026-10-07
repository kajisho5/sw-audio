#include "doctest.h"
#include "rs06/rs06.hpp"
#include "sw/fft.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::rs06;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
constexpr size_t kLat = 1024;
// dry: bursts of noise (0.4 s on, 0.8 s off) / a room: exponentially decaying noise, T60 = t60 (amplitude 60 dB down in t60), the direct sound is the first sample
std::vector<float> reverberant(double t60, double seconds, std::vector<float>* dryOut = nullptr) {
    const size_t n = static_cast<size_t>(seconds * kFs), irN = static_cast<size_t>(1.6 * kFs);
    Gauss g(7); std::vector<float> dry(n, 0.0f); for (size_t i = 0; i < n; ++i) if ((i / 19200) % 3 == 0) dry[i] = static_cast<float>(0.2 * g.gauss());
    Gauss g2(11); std::vector<double> h(irN); for (size_t i = 0; i < irN; ++i) h[i] = (i == 0 ? 1.0 : 0.25 * g2.gauss()) * std::pow(10.0, -3.0 * static_cast<double>(i) / (t60 * kFs));
    size_t N = 1; while (N < n + irN) N <<= 1; Fft f(static_cast<int>(N)); std::vector<std::complex<double>> a(N), b(N);
    for (size_t i = 0; i < n; ++i) a[i] = dry[i]; for (size_t i = 0; i < irN; ++i) b[i] = h[i];
    f.forward(a); f.forward(b); for (size_t i = 0; i < N; ++i) a[i] *= b[i]; f.inverse(a);
    std::vector<float> y(n); for (size_t i = 0; i < n; ++i) y[i] = static_cast<float>(a[i].real());
    if (dryOut) *dryOut = dry; return y;
}
// the level 0.1 .. 0.35 s after the end of the bursts (the tail), bursts 2 .. 8 (a burst every 1.2 s)
double tailDb(const std::vector<float>& y, size_t lag) { double s = 0; size_t c = 0; for (size_t k = 2; k < 9; ++k) { const size_t g0 = k * 57600 + 19200 + 4800 + lag, g1 = g0 + 12000; if (g1 > y.size()) break; for (size_t i = g0; i < g1; ++i) { s += double(y[i]) * y[i]; ++c; } } return 10 * std::log10(s / static_cast<double>(std::max<size_t>(1, c)) + 1e-30); }
}

TEST_CASE("RS06 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"rs06.reduction", "rs06.tail", "rs06.early", "rs06.smooth", "rs06.learn"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Reduction].min == -30); CHECK(s[Reduction].max == 0); CHECK(s[Reduction].def == -10);
    CHECK(s[Tail].min == 0.1); CHECK(s[Tail].max == 5); CHECK(s[Tail].def == 0.8); CHECK(s[Tail].curve == Curve::Log);
    CHECK(s[Early].labels == std::vector<std::string>{"Keep", "Reduce"}); CHECK(s[Early].def == 0);
    CHECK(s[Smooth].labels == std::vector<std::string>{"Low", "Mid", "High"}); CHECK(s[Smooth].def == 1);
    CHECK_FALSE(s[Learn].automatable);
}
TEST_CASE("RS06 reports 1024 samples; silence is silence; Reduction 0 passes the signal") {
    Processor q; CHECK(q.latencySamples() == 1024);
    { auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f); }
    auto p = make({{Reduction, 0}}); const auto x = noise(-20, 2.0, 3); const auto y = run(p, x);
    for (size_t i = 6000; i + kLat < y.size(); i += 211) NEAR(y[i + kLat], x[i], 3e-4);
}
TEST_CASE("RS06 takes the tail down and keeps the direct sound") {
    std::vector<float> dry; const auto x = reverberant(0.6, 12.0, &dry);
    auto p = make({{Reduction, -30}, {Tail, 0.6}}); const auto y = run(p, x);
    const double before = tailDb(x, 0), after = tailDb(y, kLat);
    CHECK(after < before - 6.0);
    // the start of a burst (the direct sound) keeps its level: 0.1 s from the burst start
    auto burst = [&](const std::vector<float>& s, size_t lag) { double e = 0; for (size_t k = 3; k < 8; ++k) for (size_t i = k * 57600 + 2400 + lag; i < k * 57600 + 7200 + lag; ++i) e += double(s[i]) * s[i]; return 10 * std::log10(e / 5.0 / 4800.0 + 1e-30); };
    CHECK(std::abs(burst(y, kLat) - burst(x, 0)) < 3.0);
}
TEST_CASE("RS06 Reduction sets the depth; a Tail that fits the room works better than a short one") {
    const auto x = reverberant(0.6, 12.0);
    auto after = [&](Set set) { auto p = make(set); return tailDb(run(p, x), kLat); };
    const double r10 = after({{Reduction, -10}, {Tail, 0.6}}), r30 = after({{Reduction, -30}, {Tail, 0.6}});
    CHECK(r30 < r10 - 3.0);
    CHECK(after({{Reduction, -30}, {Tail, 0.6}}) < after({{Reduction, -30}, {Tail, 0.1}}) - 1.5);
}
TEST_CASE("RS06 Early Reduce takes more of the early part") {
    const auto x = reverberant(0.6, 12.0);
    auto after = [&](double early) { auto p = make({{Reduction, -30}, {Tail, 0.6}, {Early, early}}); return tailDb(run(p, x), kLat); };
    CHECK(after(1) < after(0));
}
TEST_CASE("RS06 Learn room measures the decay and writes Tail") {
    const auto x = reverberant(0.6, 14.0);
    auto p = make({{Tail, 0.8}}); p.setParam(Learn, 1); run(p, x); p.setParam(Learn, 0);
    int id = -1; double v = 0; const int f = p.takeParamWrite(id, v);
    CHECK(f == 7); CHECK(id == Tail); CHECK(v > 0.6 * 0.6); CHECK(v < 0.6 * 1.5);
    CHECK(p.takeParamWrite(id, v) == 0);
    // nothing to learn from: no write
    auto q = make(); q.setParam(Learn, 1); run(q, noise(-20, 4.0, 3)); q.setParam(Learn, 0); CHECK(q.takeParamWrite(id, v) == 0);
}
TEST_CASE("RS06 loud input stays finite") {
    auto p = make({{Reduction, -30}, {Smooth, 2}}); auto x = noise(6, 2.0, 9); for (auto& v : x) v *= 8.0f; for (float v : run(p, x)) CHECK(std::isfinite(v));
}
