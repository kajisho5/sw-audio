#include "doctest.h"
#include "lv04/lv04.hpp"
#include <cmath>
#include <random>
#include <vector>
using namespace sw;
using namespace sw::lv04;
namespace {
const double kPi = 3.14159265358979323846, kFs = 48000.0;
std::vector<float> runBlock(Processor& p, std::vector<float> l, int block = 128) {
    std::vector<float> r = l;
    for (size_t off = 0; off < l.size(); off += static_cast<size_t>(block)) { const int n = static_cast<int>(std::min<size_t>(static_cast<size_t>(block), l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); }
    return l;
}
double rmsDb(const std::vector<float>& x, size_t from) { double s = 0; for (size_t i = from; i < x.size(); ++i) s += x[i] * x[i]; return 10 * std::log10(s / (x.size() - from)); }
}

TEST_CASE("LV04 parameter table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Mode].labels == std::vector<std::string>{"Zero", "True peak"}); CHECK(s[Mode].def == 0.0);
    CHECK(s[Ceiling].def == -1.0); CHECK(std::string(s[Ceiling].unit) == "dBFS");
    CHECK(s[Release].min == 10.0); CHECK(s[Release].max == 1000.0); CHECK(s[Release].def == 50.0);
    CHECK(s[RmsLimit].min == -20.0); CHECK(s[RmsLimit].def == -6.0);
    CHECK(std::string(s[Subsonic].minLabel) == "Off"); CHECK(s[Subsonic].def == 30.0);
}
TEST_CASE("Zero mode has no latency and never lets a sample over the ceiling") {
    Processor p; p.prepare(kFs, 128); p.snapToTargets();
    CHECK(p.latencySamples() == 0);
    std::mt19937 rng(3); std::normal_distribution<double> nd(0, 0.2); std::uniform_real_distribution<double> u(0, 1);
    std::vector<float> x(48000);
    for (auto& v : x) v = static_cast<float>(nd(rng) * (u(rng) < 0.003 ? 20.0 : 1.0));
    const double ceil = std::pow(10.0, -1.0 / 20.0);
    for (float v : runBlock(p, x)) REQUIRE(std::abs(v) <= ceil + 1e-7);
}
TEST_CASE("True peak mode reports 1.5 ms look-ahead plus detector delay and margin") {
    Processor p; p.setParam(Mode, 1); p.prepare(kFs, 128);
    CHECK(p.latencySamples() == 72 + 16);
}
TEST_CASE("RMS limit holds a loud steady tone at ceiling + RMS limit") {
    Processor p; p.setParam(Subsonic, 20); p.prepare(kFs, 128); p.snapToTargets();
    std::vector<float> x(48000 * 6);
    for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(std::sin(2 * kPi * 440.0 * i / kFs));  // RMS -3 dBFS
    CHECK(std::abs(rmsDb(runBlock(p, x), 48000 * 4) - (-1.0 - 6.0)) < 0.5);
}
TEST_CASE("every limit event is logged with its start time") {
    Processor p; p.setParam(RmsLimit, 0); p.prepare(kFs, 128); p.snapToTargets();
    std::vector<float> x(48000 * 3, 0.0f);
    for (int b : {10000, 50000, 100000}) for (int i = 0; i < 480; ++i) x[static_cast<size_t>(b + i)] = static_cast<float>(1.5 * std::sin(2 * kPi * 1000.0 * i / kFs));
    runBlock(p, x);
    REQUIRE(p.eventCount() == 3);
    CHECK(std::abs(p.event(0).startSample - 10000) < 48);
    CHECK(std::abs(p.event(2).startSample - 100000) < 48);
    CHECK(p.event(1).maxReductionDb < -2.0);
}
TEST_CASE("Subsonic filter removes 10 Hz rumble; Off leaves it") {
    auto level = [](double sub) {
        Processor p; p.setParam(Subsonic, sub); p.setParam(RmsLimit, 0); p.setParam(Ceiling, 0); p.prepare(kFs, 128); p.snapToTargets();
        std::vector<float> x(48000 * 2);
        for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(0.1 * std::sin(2 * kPi * 10.0 * i / kFs));
        return rmsDb(runBlock(p, x), 48000);
    };
    CHECK(level(30.0) < level(20.0) - 12.0);
    CHECK(level(20.0) == doctest::Approx(-23.01).epsilon(0.01));
}
