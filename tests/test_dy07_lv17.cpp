#include "doctest.h"
#include "dy07/dy07.hpp"
#include "lv17/lv17.hpp"
#include <cmath>
#include <random>
#include <vector>
using namespace sw;
namespace {
const double kPi = 3.14159265358979323846, kFs = 48000.0;
template <class P> std::vector<float> run(P& p, std::vector<float> l) { std::vector<float> r = l; for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } return l; }
double rmsDb(const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += x[i] * x[i]; return 10 * std::log10(s / (b - a)); }
std::vector<float> sine(double rmsDbfs, int n, double f = 1000) { const double a = std::pow(10.0, (rmsDbfs + 3.0103) / 20); std::vector<float> x(static_cast<size_t>(n)); for (int i = 0; i < n; ++i) x[static_cast<size_t>(i)] = static_cast<float>(a * std::sin(2 * kPi * f * i / kFs)); return x; }
std::vector<float> drums() { std::mt19937 rng(5); std::normal_distribution<double> nd(0, 1); std::vector<float> x(96000); for (size_t i = 0; i < x.size(); ++i) { const double t = (i % 12000) / kFs; x[i] = static_cast<float>(0.5 * std::exp(-t * 25) * (std::sin(2 * kPi * 70 * t) + 0.3 * nd(rng))); } return x; }
double peakDb(const std::vector<float>& y, size_t from) { double pk = 0; for (size_t i = from; i < y.size(); ++i) pk = std::max(pk, (double)std::abs(y[i])); return 20 * std::log10(pk); }
}

TEST_CASE("DY07 table follows 04_parameters ranges") {
    using namespace dy07; const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Threshold].min == -40); CHECK(s[Threshold].max == 20);
    CHECK(s[Compress].min == 1); CHECK(std::string(s[Compress].maxLabel) == "inf");
    CHECK(s[Output].min == -20); CHECK(s[Output].max == 20);
    CHECK(s[Snap].min == -6); CHECK(s[Snap].max == 6); CHECK(s[Snap].def == 0);
    CHECK(s[Knee].labels == std::vector<std::string>{"Soft knee", "Hard knee"});
}
TEST_CASE("DY07 static curve: threshold 0 dB = -18 dBFS, 4:1, hard knee, RMS detection") {
    using namespace dy07;
    Processor p; p.setParam(Threshold, 0); p.setParam(Compress, 4); p.setParam(Knee, 1); p.prepare(kFs, 256); p.snapToTargets();
    CHECK(rmsDb(run(p, sine(-8, 48000)), 24000, 48000) == doctest::Approx(-8 - 7.5).epsilon(0.02));
}
TEST_CASE("DY07 release recovers at a constant 120 dB/s") {
    using namespace dy07;
    Processor p; p.setParam(Threshold, -20); p.setParam(Compress, 20); p.setParam(Knee, 1); p.prepare(kFs, 256); p.snapToTargets();
    auto x = sine(-6, 48000); auto q = sine(-60, 48000); x.insert(x.end(), q.begin(), q.end());
    run(p, x);
    // after the loud part GR was about -(32 - 1.6) dB; measure how long the meter took to come back to -1 dB
    CHECK(p.recoveryMs() == doctest::Approx((31.0 - 1.0) / 120.0 * 1000.0).epsilon(0.15));
}
TEST_CASE("DY07 Snap shapes the attack: +6 louder heads, -6 softer heads") {
    using namespace dy07;
    auto heads = [](double snap) { Processor p; p.setParam(Threshold, 20); p.setParam(Snap, snap); p.prepare(kFs, 256); p.snapToTargets(); return peakDb(run(p, drums()), 48000); };
    const double zero = heads(0);
    CHECK(heads(6) > zero + 2.0);
    CHECK(heads(-6) < zero - 2.0);
}
TEST_CASE("LV17 table uses the screen values as defaults") {
    using namespace lv17; const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Mode].labels == std::vector<std::string>{"Speech", "Music", "Band"});
    CHECK(s[Threshold].def == -20); CHECK(s[Ratio].def == 3); CHECK(s[Attack].def == 10);
    CHECK(std::string(s[Release].maxLabel) == "Auto"); CHECK(s[Release].def == s[Release].max); CHECK(s[Makeup].def == 3);
}
TEST_CASE("LV17 Speech mode: RMS static curve plus default makeup") {
    using namespace lv17;
    Processor p; p.prepare(kFs, 256); p.snapToTargets();
    // -10 dBFS RMS, 10 dB over a -20 threshold with a 6 dB knee, 3:1 -> -6.67 dB, then +3 makeup
    CHECK(rmsDb(run(p, sine(-10, 48000)), 24000, 48000) == doctest::Approx(-10 - 6.67 + 3).epsilon(0.03));
}
TEST_CASE("LV17 Band mode (peak detection) compresses a steady tone harder than Speech (RMS)") {
    using namespace lv17;
    auto out = [](double mode) { Processor p; p.setParam(Mode, mode); p.prepare(kFs, 256); p.snapToTargets(); return rmsDb(run(p, sine(-10, 48000)), 24000, 48000); };
    CHECK(out(2) < out(0) - 1.5);
}
