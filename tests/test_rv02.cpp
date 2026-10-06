#include "doctest.h"
#include "rv02/rv02.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::rv02;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}, double bpm = 0) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); if (bpm > 0) p.setTempo(bpm); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> impulse(Processor& p, double sec) { std::vector<float> x(static_cast<size_t>(sec * kFs), 0.0f); x[0] = 1.0f; return run(p, x); }
double crossing(const std::vector<float>& h, double db) {
    double tot = 0; for (float v : h) tot += double(v) * v; double e = tot;
    for (size_t i = 0; i < h.size(); ++i) { if (10 * std::log10(e / tot + 1e-30) <= -db) return static_cast<double>(i) / kFs; e -= double(h[i]) * h[i]; }
    return static_cast<double>(h.size()) / kFs;
}
double rt60(const std::vector<float>& h) { return 3.0 * (crossing(h, 30.0) - crossing(h, 10.0)); }
double band(const std::vector<float>& y, double f0, double f1, size_t a, size_t b) { double s = 0; int n = 0; for (double f = f0; f <= f1; f *= 1.1) { s += std::pow(10.0, binDb(y, f, a, b) / 10.0); ++n; } return 10 * std::log10(s / n + 1e-30); }
std::vector<float> burst(double rmsDbfs, double sec, double total, unsigned seed = 1) { auto x = noise(rmsDbfs, sec, seed); x.resize(static_cast<size_t>(total * kFs), 0.0f); return x; }
}

TEST_CASE("RV02 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"rv02.decay", "rv02.predelay", "rv02.damping", "rv02.lowcut", "rv02.width", "rv02.mix", "rv02.monoin", "rv02.sync", "rv02.evo.on"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Decay].min == 0.5); CHECK(s[Decay].max == 6); CHECK(s[Decay].def == 2.0); CHECK(s[Decay].curve == Curve::Log);
    CHECK(s[PreDelay].max == 200); CHECK(s[PreDelay].def == 20); CHECK(s[PreDelay].curve == Curve::Skew); CHECK(s[PreDelay].skew == 2);
    CHECK(s[Damping].def == 50); CHECK(std::string(s[Damping].minLabel) == "Dark"); CHECK(std::string(s[Damping].maxLabel) == "Bright");
    CHECK(s[LowCut].min == 20); CHECK(s[LowCut].max == 500); CHECK(s[LowCut].def == 80);
    CHECK(s[Width].def == 100); CHECK(std::string(s[Width].minLabel) == "Mono"); CHECK(std::string(s[Width].maxLabel) == "Wide");
    CHECK(s[Mix].def == 30); CHECK(s[MonoIn].def == 0); CHECK(s[DuckOn].def == 0); CHECK(s[Sync].def == 0);
}
TEST_CASE("RV02 no delay is reported; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("RV02 the dispersion: the highs come first") {
    CHECK(dispersionDelay(200, kFs) > 4.0 * dispersionDelay(8000, kFs));
    CHECK(dispersionDelay(200, kFs) > dispersionDelay(1000, kFs)); CHECK(dispersionDelay(1000, kFs) > dispersionDelay(4000, kFs));
    CHECK(dispersionDelay(200, kFs) > 150.0);   // about 3 ms for the lows
}
TEST_CASE("RV02 Decay sets the reverberation time") {
    double prev = 0;
    for (double d : {0.6, 2.0, 5.0}) {
        auto p = make({{Decay, d}, {Damping, 100}, {LowCut, 20}, {PreDelay, 0}}); const auto h = impulse(p, std::max(3.0, 2.5 * d));
        const double rt = rt60(h); NEAR(rt / d, 1.0, 0.25); CHECK(rt > prev); prev = rt;
    }
}
TEST_CASE("RV02 Damping: Dark loses the highs") {
    auto bright = make({{Damping, 100}}); auto dark = make({{Damping, 0}});
    const auto x = burst(-20, 1.0, 3.0), yb = run(bright, x), yd = run(dark, x);
    const size_t a = static_cast<size_t>(1.4 * kFs), b = static_cast<size_t>(2.4 * kFs);
    CHECK(band(yb, 6000, 10000, a, b) - band(yd, 6000, 10000, a, b) > band(yb, 300, 600, a, b) - band(yd, 300, 600, a, b) + 6.0);
}
TEST_CASE("RV02 Low cut") {
    auto flat = make({{LowCut, 20}}); auto cut = make({{LowCut, 500}});
    const auto x = burst(-20, 1.0, 2.0), a = run(flat, x), b = run(cut, x);
    CHECK(band(b, 60, 120, 24000, x.size()) < band(a, 60, 120, 24000, x.size()) - 20.0);
}
TEST_CASE("RV02 Width and Mono in") {
    auto m = make({{Width, 0}}); auto w = make({{Width, 100}});
    const auto x = burst(-20, 0.3, 1.0); const auto m0 = run2(m, x, x), w0 = run2(w, x, x);
    for (size_t i = 0; i < x.size(); ++i) CHECK(std::abs(m0.first[i] - m0.second[i]) < 1e-6);
    double sl = 0, sr = 0, sx = 0; for (size_t i = 24000; i < x.size(); ++i) { sl += double(w0.first[i]) * w0.first[i]; sr += double(w0.second[i]) * w0.second[i]; sx += double(w0.first[i]) * w0.second[i]; }
    CHECK(std::abs(sx) / std::sqrt(sl * sr) < 0.5);
    // Mono in: the left-only and right-only inputs give the same result; without it they differ
    std::vector<float> z(x.size(), 0.0f);
    auto a = make({{MonoIn, 1}}); auto b = make({{MonoIn, 1}}); const auto ya = run2(a, x, z), yb = run2(b, z, x);
    for (size_t i = 0; i < x.size(); ++i) { CHECK(ya.first[i] == yb.first[i]); CHECK(ya.second[i] == yb.second[i]); }
    auto c = make({{MonoIn, 0}}); auto d = make({{MonoIn, 0}}); const auto yc = run2(c, x, z), yd = run2(d, z, x);
    double diff = 0, tot = 0; for (size_t i = 0; i < x.size(); ++i) { diff += std::pow(double(yc.first[i]) - yd.first[i], 2); tot += std::pow(double(yc.first[i]), 2); }
    CHECK(diff > 0.3 * tot);
}
TEST_CASE("RV02 Pre-delay: in ms, or a note length from the host tempo") {
    auto p = make({{PreDelay, 60}}); const auto h = impulse(p, 1.0);
    double pk = 0; for (size_t i = 0; i < static_cast<size_t>(0.059 * kFs); ++i) pk = std::max(pk, double(std::abs(h[i]))); CHECK(pk < 1e-6);
    // 1/8 at 240 bpm = 125 ms; without a tempo the Pre-delay value is used
    auto s = make({{PreDelay, 10}, {Sync, 3}}, 240.0); NEAR(s.preDelayMs(), 125.0, 0.01);
    const auto hs = impulse(s, 1.0); double pk2 = 0; for (size_t i = 0; i < static_cast<size_t>(0.124 * kFs); ++i) pk2 = std::max(pk2, double(std::abs(hs[i]))); CHECK(pk2 < 1e-6);
    auto n = make({{PreDelay, 10}, {Sync, 3}}); NEAR(n.preDelayMs(), 10.0, 0.01);
    auto q = make({{Sync, 4}}, 60.0); NEAR(q.preDelayMs(), 200.0, 0.01);   // 1/4 at 60 bpm = 1 s, capped at 200 ms
}
TEST_CASE("RV02 Duck lowers the plate while the source is loud") {
    auto off = make({{DuckOn, 0}}); auto on = make({{DuckOn, 1}});
    const auto x = burst(-8, 1.0, 3.0), a = run(off, x), b = run(on, x);
    const double loud = rmsDb(b, 24000, 48000) - rmsDb(a, 24000, 48000);
    CHECK(loud < -3.0); CHECK(loud > -9.0);
    NEAR(rmsDb(b, static_cast<size_t>(2.3 * kFs), static_cast<size_t>(2.8 * kFs)) - rmsDb(a, static_cast<size_t>(2.3 * kFs), static_cast<size_t>(2.8 * kFs)), 0.0, 1.5);
}
TEST_CASE("RV02 the tail is dense and the level is sensible; the longest decay stays finite") {
    auto p = make({{PreDelay, 0}}); const auto h = impulse(p, 3.0);
    double e = 0; for (float v : h) e += double(v) * v; CHECK(e > 0.4); CHECK(e < 2.0);
    auto q = make({{Decay, 6}}); for (float v : run(q, burst(-6, 1.0, 4.0))) CHECK(std::isfinite(v));
}
