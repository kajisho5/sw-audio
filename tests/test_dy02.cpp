#include "doctest.h"
#include "near.hpp"
#include "dy02/dy02.hpp"
#include <cmath>
#include <vector>
using namespace sw;
using namespace sw::dy02;
namespace {
const double kPi = 3.14159265358979323846, kFs = 48000.0;
Processor make(std::vector<std::pair<int, double>> set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> run(Processor& p, std::vector<float> l) {
    std::vector<float> r = l;
    for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); }
    return l;
}
std::vector<float> sine(double rmsDbfs, double seconds, double f = 1000) { const double a = std::pow(10.0, (rmsDbfs + 3.0103) / 20); const int n = static_cast<int>(seconds * kFs); std::vector<float> x(static_cast<size_t>(n)); for (int i = 0; i < n; ++i) x[static_cast<size_t>(i)] = static_cast<float>(a * std::sin(2 * kPi * f * i / kFs)); return x; }
double rmsDb(const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += static_cast<double>(x[i]) * x[i]; return 10 * std::log10(s / (b - a) + 1e-20); }
double lastRms(const std::vector<float>& y) { return rmsDb(y, y.size() - 4800, y.size()); }
// ms until the gain reduction has recovered to `frac` of its value at the end of the loud part
double recoveryMs(Processor& p, double frac) {
    const double g0 = p.gainReductionDb(); const auto q = sine(-70, 20.0);
    for (size_t off = 0; off + 48 <= q.size(); off += 48) { std::vector<float> a(q.begin() + off, q.begin() + off + 48), b = a; float* c[2] = {a.data(), b.data()}; p.process(c, 2, 48); if (p.gainReductionDb() > g0 * (1.0 - frac)) return 1000.0 * (off + 48) / kFs; }
    return 1e9;
}
}

TEST_CASE("DY02 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"dy02.level", "dy02.out", "dy02.speed", "dy02.target", "dy02.emph", "dy02.mix", "dy02.evo.on", "dy02.automakeup"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Level].min == 0); CHECK(s[Level].max == 10); CHECK(s[Level].def == 0);
    CHECK(s[Output].min == -12); CHECK(s[Output].max == 24); CHECK(s[Output].def == 0);
    CHECK(s[Speed].labels == std::vector<std::string>{"Fast", "Prog", "Slow"}); CHECK(s[Speed].def == 1);
    CHECK(s[Target].min == -30); CHECK(s[Target].max == -6); CHECK(s[Target].def == -18);
    CHECK(s[Emphasis].min == 0); CHECK(s[Emphasis].max == 12); CHECK(s[Emphasis].def == 0);
    CHECK(s[Mix].def == 100);
    CHECK(s[Ride].labels == std::vector<std::string>{"Off", "On"}); CHECK(s[Ride].def == 0);
    CHECK(s[AutoMakeup].labels == std::vector<std::string>{"Off", "On"}); CHECK(s[AutoMakeup].def == 0);
    for (const auto& p : s) CHECK(p.automatable);
}
TEST_CASE("DY02 Level 0..10 moves the threshold 0..-40 dBFS: untouched at 0, 3:1 soft law above") {
    auto quiet = make(); CHECK(lastRms(run(quiet, sine(-10, 3))) == doctest::Approx(-10.0).epsilon(0.01));  // Level 0: threshold 0 dBFS
    auto p = make({{Level, 5}});                                                                           // threshold -20 dBFS
    // -10 dBFS RMS: 10 dB over, ratio 3:1 -> about 6.7 dB of reduction
    const double out = lastRms(run(p, sine(-10, 3)));
    NEAR(out, -10.0 - 6.7, 1.0);
}
TEST_CASE("DY02 release has two stages: half comes back fast, the rest slowly") {
    auto rel = [](int speed, double frac) { auto p = make({{Level, 5}, {Speed, static_cast<double>(speed)}}); run(p, sine(-10, 4)); return recoveryMs(p, frac); };
    CHECK(rel(0, 0.5) < rel(2, 0.5));                       // Fast (40 ms) < Slow (200 ms)
    NEAR(rel(0, 0.5), 78.0, 25.0);                          // 0.5 (e^-t/40 + e^-t/500) = 0.5 at about 78 ms: the fast stage is gone
    CHECK(rel(0, 0.9) > rel(0, 0.5) * 4.0);                 // then the slow stage (0.5 s) takes over
    CHECK(rel(2, 0.9) > rel(0, 0.9) * 2.0);                 // Slow's slow stage is 3 s
}
TEST_CASE("DY02 Prog: the slow stage stretches with the depth and length of the previous compression") {
    auto slow = [](double secs) { auto p = make({{Level, 8}, {Speed, 1}}); run(p, sine(-8, secs)); return recoveryMs(p, 0.9); };
    CHECK(slow(10.0) > slow(0.3) * 1.5);
}
TEST_CASE("DY02 Emphasis makes bright sounds compress earlier (detector only)") {
    auto gr = [](double emph, double f) { auto p = make({{Level, 8}, {Emphasis, emph}}); run(p, sine(-34, 3, f)); return p.gainReductionDb(); };
    NEAR(gr(0, 6000), gr(0, 200), 0.5);
    CHECK(gr(12, 6000) < gr(12, 200) - 3.0);
    CHECK(gr(12, 6000) < gr(0, 6000) - 3.0);
}
TEST_CASE("DY02 Ride moves the short-term loudness toward Target, +-12 dB, not in silence") {
    auto level = [](double inRms, int ride) { auto p = make({{Ride, static_cast<double>(ride)}, {Target, -18}}); return lastRms(run(p, sine(inRms, 14))); };
    NEAR(level(-26, 0), -26.0, 0.2);
    NEAR(level(-26, 1), -18.0 - 2.32, 1.5);        // identical L/R: BS.1770 adds 3.01 dB, so -18 LUFS = -20.3 dBFS RMS per channel
    NEAR(level(-45, 1), -45.0 + 12.0, 1.0);       // capped at +12 dB
    NEAR(level(-12, 1), -18.0 - 2.32, 1.5);        // and pulls loud material down
    NEAR(level(-60, 1), -60.0, 0.5);              // breath / silence: ride holds
}
TEST_CASE("DY02 Auto makeup restores the average gain reduction") {
    auto lvl = [](int mk) { auto p = make({{Level, 6}, {AutoMakeup, static_cast<double>(mk)}}); return lastRms(run(p, sine(-10, 12))); };
    auto g = make({{Level, 6}}); run(g, sine(-10, 12));
    NEAR(lvl(1) - lvl(0), -g.gainReductionDb(), 1.5);
    CHECK(lvl(1) - lvl(0) > 4.0);
}
TEST_CASE("DY02 silence stays silent, extreme input stays finite, latency 0") {
    auto p = make({{Level, 10}, {Ride, 1}, {AutoMakeup, 1}, {Emphasis, 12}});
    for (float v : run(p, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f);
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : run(p, x)) REQUIRE(std::isfinite(v));
    CHECK(p.latencySamples() == 0);
}
