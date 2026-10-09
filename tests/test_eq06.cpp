#include "doctest.h"
#include "os_helpers.hpp"
#include "eq06/eq06.hpp"
#include <cmath>
#include <complex>
#include <vector>
using namespace sw;
using namespace sw::eq06;
namespace {
const double kPi = 3.14159265358979323846, kFs = 48000.0;
double at(std::vector<std::pair<int, double>> set, double f) {
    Processor p; p.setParam(Drive, 0); for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets();
    const int n = 48000; std::vector<float> l(n), r(n);
    for (int i = 0; i < n; ++i) l[i] = r[i] = static_cast<float>(0.001 * std::sin(2 * kPi * f * i / kFs));
    for (int off = 0; off < n; off += 256) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, std::min(256, n - off)); }
    std::complex<double> acc; double w = 0;
    for (int i = n / 2; i < n; ++i) { double h = 0.5 - 0.5 * std::cos(2 * kPi * (i - n / 2) / (n / 2 - 1)); acc += h * l[i] * std::exp(std::complex<double>(0, -2 * kPi * f * i / kFs)); w += h; }
    return 20 * std::log10(2 * std::abs(acc) / w / 0.001);
}
}

TEST_CASE("EQ06 parameter table follows the spec (13 gain steps of 2 dB)") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[LowHz].steps == std::vector<double>{30, 50, 100, 200, 300, 400}); CHECK(s[LowHz].def == 100);
    CHECK(s[MidHz].steps == std::vector<double>{400, 800, 1500, 3000, 5000, 8000}); CHECK(s[MidHz].def == 1500);
    CHECK(s[HighHz].steps == std::vector<double>{2500, 5000, 7500, 10000, 12500, 15000}); CHECK(s[HighHz].def == 10000);
    CHECK(s[MidGain].numSteps() == 13); CHECK(s[MidGain].steps.front() == -12); CHECK(s[MidGain].steps.back() == 12);
    CHECK(s[Shape].labels == std::vector<std::string>{"Peak", "Shelf"}); CHECK(s[Drive].def == 2);
}
TEST_CASE("gains snap to the 2 dB grid") {
    Processor p; p.setParam(MidGain, 5.2); CHECK(p.getParam(MidGain) == 6.0);
}
TEST_CASE("Mid +12 dB at 1.5 kHz peaks at +12 dB") { CHECK(std::abs(at({{MidGain, 12}}, 1500.0) - 12.0) < 0.05); }
TEST_CASE("proportional Q: a big boost is narrower than a small one") {
    const double small = at({{MidGain, 2}}, 3000.0) / 2.0, big = at({{MidGain, 12}}, 3000.0) / 12.0;  // one octave above, as a fraction of the peak
    CHECK(big < small - 0.15);
}
TEST_CASE("Shape Shelf turns Low and High into shelves") {
    CHECK(std::abs(at({{LowGain, 8}, {Shape, 1}}, 25.0) - 8.0) < 0.3);
    CHECK(at({{LowGain, 8}}, 25.0) < 4.0);  // the peak version falls away far below 100 Hz
}
TEST_CASE("Glide: a -12 to +12 jump does not click") {
    Processor p; p.setParam(Drive, 0); p.setParam(LowGain, -12); p.prepare(kFs, 64); p.snapToTargets();
    const int n = 24000; std::vector<float> l(n), r(n);
    for (int i = 0; i < n; ++i) l[i] = r[i] = static_cast<float>(0.2 * std::sin(2 * kPi * 100.0 * i / kFs));
    for (int off = 0; off < n; off += 64) { if (off == 9600) p.setParam(LowGain, 12); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, 64); }
    double tr = 0, st = 0;
    for (int i = 9601; i < 12000; ++i) tr = std::max(tr, (double)std::abs(l[i] - l[i - 1]));
    for (int i = 18001; i < n; ++i) st = std::max(st, (double)std::abs(l[i] - l[i - 1]));
    CHECK(tr <= 1.2 * st);
}

// the common oversampling setting (spec 共通機能: 1x / 2x / 4x, default 2x)
TEST_CASE("EQ06: the oversampling parameter is the last one, 1x / 2x / 4x, default 2x, and the Drive stage follows it") {
    const auto& s = eq06::specs();
    REQUIRE(s[eq06::Oversample].steps == std::vector<double>{1, 2, 4});
    CHECK(std::string(s[eq06::Oversample].id) == "eq06.os"); CHECK(s[eq06::Oversample].def == 2.0); CHECK(static_cast<int>(s.size()) - 2 == eq06::Oversample);   // Unit A / B / C follows
    auto alias = [](int os) { eq06::Processor p; p.setParam(eq06::Drive, 10); p.setParam(eq06::Oversample, os); p.prepare(ost::kFs, 256); p.snapToTargets(); return ost::relDb(p, 15000, 3000, 0.3); };
    const double a1 = alias(1), a2 = alias(2), a4 = alias(4);
    INFO("15 kHz at Drive 10, alias at 3 kHz: 1x " << a1 << " dB, 2x " << a2 << " dB, 4x " << a4 << " dB");
    CHECK(a1 > -45.0); CHECK(a2 < -60.0); CHECK(ost::notWorse(a4, a2));
}
