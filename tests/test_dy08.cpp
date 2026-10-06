#include "doctest.h"
#include "dy08/dy08.hpp"
#include <cmath>
#include <random>
#include <vector>
using namespace sw;
using namespace sw::dy08;
namespace {
const double kPi = 3.14159265358979323846, kFs = 48000.0;
double rmsDbTail(const std::vector<float>& x) { double s = 0; size_t n0 = x.size() / 2; for (size_t i = n0; i < x.size(); ++i) s += x[i] * x[i]; return 10 * std::log10(s / (x.size() - n0)); }
std::vector<float> runSine(Processor& p, double peak, int n = 48000) {
    std::vector<float> l(static_cast<size_t>(n)), r;
    for (int i = 0; i < n; ++i) l[static_cast<size_t>(i)] = static_cast<float>(peak * std::sin(2 * kPi * 1000.0 * i / kFs));
    r = l;
    for (int off = 0; off < n; off += 256) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, std::min(256, n - off)); }
    return l;
}
}

TEST_CASE("DY08 parameter table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Threshold].min == -60.0); CHECK(s[Threshold].max == 0.0); CHECK(s[Threshold].def == 0.0);
    CHECK(s[Ratio].min == 1.0); CHECK(s[Ratio].max == 20.0); CHECK(s[Ratio].def == 2.0); CHECK(s[Ratio].curve == Curve::Log);
    CHECK(std::string(s[Ratio].maxLabel) == "inf");
    CHECK(s[Knee].max == 24.0); CHECK(s[Knee].def == 6.0);
    CHECK(s[Attack].min == 0.05); CHECK(s[Attack].max == 200.0); CHECK(s[Attack].def == 10.0); CHECK(s[Attack].curve == Curve::Skew);
    CHECK(s[Release].min == 5.0); CHECK(s[Release].max == 3000.0); CHECK(s[Release].def == 150.0);
    CHECK(s[Makeup].min == -12.0); CHECK(s[Makeup].max == 24.0);
    CHECK(s[Mix].def == 100.0);
    CHECK(std::string(s[ScHpf].minLabel) == "Off"); CHECK(s[ScHpf].def == s[ScHpf].min);
    CHECK(s[Detector].labels == std::vector<std::string>{"Peak", "RMS", "Program"}); CHECK(s[Detector].def == 2.0);
    CHECK(s[Lookahead].def == 0.0);
}
TEST_CASE("default settings (threshold 0 dB) do not compress a -6 dBFS sine") {
    Processor p; p.prepare(kFs, 256); p.snapToTargets();
    CHECK(rmsDbTail(runSine(p, 0.5)) == doctest::Approx(20 * std::log10(0.5) - 3.01).epsilon(0.005));
}
TEST_CASE("static curve: RMS detector, -20 dB threshold, 4:1, hard knee") {
    Processor p; p.prepare(kFs, 256);
    p.setParam(Threshold, -20); p.setParam(Ratio, 4); p.setParam(Knee, 0); p.setParam(Detector, 1);
    p.setParam(Attack, 1); p.setParam(Release, 50); p.snapToTargets();
    // input RMS -13.01 dB -> 6.99 dB over -> -5.24 dB of gain reduction
    CHECK(rmsDbTail(runSine(p, std::pow(10.0, -10.0 / 20))) == doctest::Approx(-13.01 - 5.24).epsilon(0.01));
}
TEST_CASE("Makeup adds gain") {
    Processor p; p.prepare(kFs, 256); p.setParam(Makeup, 6.0); p.snapToTargets();
    CHECK(rmsDbTail(runSine(p, 0.1)) == doctest::Approx(-20 - 3.01 + 6).epsilon(0.005));
}
TEST_CASE("lookahead reports 5 ms of latency and delays the audio by exactly that") {
    Processor p; p.setParam(Lookahead, 1); p.prepare(kFs, 256); p.snapToTargets();
    CHECK(p.latencySamples() == 240);
    std::vector<float> l(1024, 0.0f), r(1024, 0.0f); l[10] = r[10] = 0.01f;
    float* c[2] = {l.data(), r.data()}; p.process(c, 2, 1024);
    CHECK(l[250] == doctest::Approx(0.01f)); CHECK(l[10] == 0.0f);
}
TEST_CASE("latency follows the Lookahead switch before the next prepare (host restart)") {
    Processor p; p.prepare(kFs, 256);
    CHECK(p.latencySamples() == 0);
    p.setParam(Lookahead, 1);
    CHECK(p.latencySamples() == 240);  // desired latency; applied on the next prepare
}
TEST_CASE("Auto release follows the host tempo (16th note), 150 ms without tempo") {
    Processor p; p.prepare(kFs, 256); p.setParam(AutoRelease, 1); p.snapToTargets();
    CHECK(p.effectiveReleaseMs() == doctest::Approx(150.0));
    p.setTempo(120.0);
    CHECK(p.effectiveReleaseMs() == doctest::Approx(125.0));
    p.setParam(AutoRelease, 0);
    CHECK(p.effectiveReleaseMs() == doctest::Approx(150.0));  // manual Release default
}
TEST_CASE("random automation stays finite") {
    Processor p; p.prepare(44100.0, 128);
    std::mt19937 rng(9); std::uniform_real_distribution<double> u(0, 1); std::uniform_real_distribution<float> d(-1, 1);
    std::vector<float> l(128), r(128); bool ok = true;
    for (int b = 0; b < 3000; ++b) {
        for (int id = 0; id < kNumParams; ++id) if (u(rng) < 0.2) p.setParam(id, specs()[static_cast<size_t>(id)].toValue(u(rng)));
        for (int i = 0; i < 128; ++i) { l[i] = d(rng) * 2; r[i] = d(rng); }
        float* c[2] = {l.data(), r.data()}; p.process(c, 2, 128);
        for (int i = 0; i < 128; ++i) if (!std::isfinite(l[i]) || !std::isfinite(r[i]) || std::abs(l[i]) > 100) ok = false;
    }
    CHECK(ok);
}
TEST_CASE("External sidechain drives the compression; Internal ignores it") {
    auto run = [](double sideSel) {
        Processor p; p.setParam(Threshold, -30); p.setParam(Ratio, 10); p.setParam(Detector, 1); p.setParam(Sidechain, sideSel);
        p.prepare(kFs, 256); p.snapToTargets();
        std::vector<float> l(48000), r(48000), sl(48000), sr;
        for (int i = 0; i < 48000; ++i) { l[i] = static_cast<float>(0.01 * std::sin(2 * kPi * 300.0 * i / kFs)); sl[i] = static_cast<float>(0.8 * std::sin(2 * kPi * 100.0 * i / kFs)); }
        r = l; sr = sl;
        for (int off = 0; off < 48000; off += 256) {
            float* c[2] = {l.data() + off, r.data() + off}; const float* s[2] = {sl.data() + off, sr.data() + off};
            p.processWithSidechain(c, 2, std::min(256, 48000 - off), s, 2);
        }
        return rmsDbTail(l);
    };
    const double quiet = 20 * std::log10(0.01) - 3.01;
    CHECK(run(0) == doctest::Approx(quiet).epsilon(0.005));      // Internal: the quiet signal stays uncompressed
    CHECK(run(1) < quiet - 15.0);                                // External: the loud key pulls it down
}
