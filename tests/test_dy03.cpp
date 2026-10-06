#include "doctest.h"
#include "dy03/dy03.hpp"
#include <cmath>
#include <random>
#include <vector>
using namespace sw;
using namespace sw::dy03;
namespace {
const double kPi = 3.14159265358979323846, kFs = 48000.0;
std::vector<float> run(Processor& p, std::vector<float> l) { std::vector<float> r = l; for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } return l; }
double rmsDb(const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += x[i] * x[i]; return 10 * std::log10(s / (b - a)); }
}

TEST_CASE("DY03 parameter table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Threshold].min == -30); CHECK(s[Threshold].def == 0);
    CHECK(s[Ratio].steps == std::vector<double>{1.5, 2, 4, 10}); CHECK(s[Ratio].def == 2);
    CHECK(s[Attack].steps == std::vector<double>{0.1, 0.3, 1, 3, 10, 30, 100}); CHECK(s[Attack].def == 10);
    CHECK(s[Release].labels.back() == "Auto"); CHECK(s[Release].def == s[Release].steps.back());
    CHECK(s[Makeup].max == 20); CHECK(s[Knee].max == 12); CHECK(std::string(s[ScHpf].minLabel) == "Off"); CHECK(s[PunchKeep].def == 0);
}
TEST_CASE("static curve on a steady tone: -20 dB threshold, 4:1") {
    Processor p; p.setParam(Threshold, -20); p.setParam(Ratio, 4); p.setParam(Attack, 0.1); p.setParam(Release, 50); p.prepare(kFs, 256); p.snapToTargets();
    std::vector<float> x(48000); for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(std::pow(10.0, -10.0 / 20) * std::sin(2 * kPi * 1000.0 * i / kFs));
    CHECK(rmsDb(run(p, x), 24000, 48000) == doctest::Approx(-13.01 - 5.24).epsilon(0.012));
}
TEST_CASE("Auto release recovers fast after a short burst and slowly after sustained compression") {
    auto recoverMs = [](int burstMs) {
        Processor p; p.setParam(Threshold, -30); p.setParam(Ratio, 10); p.setParam(Attack, 0.1); p.prepare(kFs, 256); p.snapToTargets();
        const int b = burstMs * 48, n = b + 48000 * 3; std::vector<float> x(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) x[static_cast<size_t>(i)] = static_cast<float>((i < b ? 0.9 : 0.02) * std::sin(2 * kPi * 500.0 * i / kFs));
        const auto y = run(p, x);
        for (int t = b; t + 480 < n; t += 48) {  // first 10 ms window back within 1 dB of the uncompressed quiet level
            double s = 0; for (int i = t; i < t + 480; ++i) s += y[static_cast<size_t>(i)] * y[static_cast<size_t>(i)];
            if (10 * std::log10(s / 480) > 20 * std::log10(0.02) - 3.01 - 1.0) return (t - b) / 48.0;
        }
        return 1e9;
    };
    const double shortMs = recoverMs(40), longMs = recoverMs(2000);
    CHECK(shortMs < 400.0);
    CHECK(longMs > 2.0 * shortMs);
}
TEST_CASE("Punch keep lets drum heads through the compression") {
    auto heads = [](double punch) {
        Processor p; p.setParam(Threshold, -30); p.setParam(Ratio, 10); p.setParam(Attack, 0.1); p.setParam(Release, 200); p.setParam(PunchKeep, punch);
        p.prepare(kFs, 256); p.snapToTargets();
        std::mt19937 rng(5); std::normal_distribution<double> nd(0, 1);
        std::vector<float> x(48000 * 2);
        for (size_t i = 0; i < x.size(); ++i) { const double t = (i % 12000) / kFs; x[i] = static_cast<float>(0.8 * std::exp(-t * 25) * (std::sin(2 * kPi * 70 * t) + 0.3 * nd(rng))); }
        const auto y = run(p, x);
        double pk = 0; for (size_t i = 48000; i < y.size(); ++i) pk = std::max(pk, (double)std::abs(y[i]));
        return 20 * std::log10(pk);
    };
    CHECK(heads(1) > heads(0) + 2.0);
}
