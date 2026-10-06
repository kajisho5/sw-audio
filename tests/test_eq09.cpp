#include "doctest.h"
#include "eq09/eq09.hpp"
#include <cmath>
#include <complex>
#include <vector>
using namespace sw;
using namespace sw::eq09;
namespace {
const double kPi = 3.14159265358979323846, kFs = 48000.0;
double gainDb(Processor& p, double f) {
    const int n = 48000; std::vector<float> l(n), r(n);
    for (int i = 0; i < n; ++i) l[i] = r[i] = static_cast<float>(0.01 * std::sin(2 * kPi * f * i / kFs));
    for (int off = 0; off < n; off += 256) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, std::min(256, n - off)); }
    std::complex<double> acc; double w = 0;
    for (int i = n / 2; i < n; ++i) { double h = 0.5 - 0.5 * std::cos(2 * kPi * (i - n / 2) / (n / 2 - 1)); acc += h * l[i] * std::exp(std::complex<double>(0, -2 * kPi * f * i / kFs)); w += h; }
    return 20 * std::log10(2 * std::abs(acc) / w / 0.01);
}
double at(std::vector<std::pair<int, double>> set, double f) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return gainDb(p, f); }
}

TEST_CASE("EQ09 parameter table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Tilt].min == -6.0); CHECK(s[Tilt].max == 6.0);
    CHECK(s[PivotHz].steps == std::vector<double>{300, 600, 1000, 2000, 4000}); CHECK(s[PivotHz].def == 1000.0);
    CHECK(s[LowLift].max == 10.0); CHECK(s[Air].max == 10.0); CHECK(s[Output].min == -10.0);
    CHECK(s[AutoPivot].def == 0.0);
}
TEST_CASE("flat by default") { for (double f : {30.0, 1000.0, 15000.0}) CHECK(std::abs(at({}, f)) < 0.02); }
TEST_CASE("Tilt +6: lows go down, highs go up, the pivot stays put") {
    CHECK(std::abs(at({{Tilt, 6}}, 1000.0)) < 0.05);
    CHECK(at({{Tilt, 6}}, 25.0) == doctest::Approx(-6.0).epsilon(0.08));
    CHECK(at({{Tilt, 6}}, 16000.0) == doctest::Approx(6.0).epsilon(0.08));
}
TEST_CASE("Low lift 10 is +6 dB at the bottom, Air 10 about +6 dB at the top") {
    CHECK(at({{LowLift, 10}}, 25.0) == doctest::Approx(6.0).epsilon(0.05));
    CHECK(at({{Air, 10}}, 19000.0) == doctest::Approx(6.0).epsilon(0.12));
}
TEST_CASE("Auto pivot follows the spectral centre of the material") {
    for (double f : {700.0, 2500.0}) {
        Processor p; p.setParam(AutoPivot, 1); p.prepare(kFs, 256); p.snapToTargets();
        std::vector<float> l(256), r(256);
        for (int b = 0; b < 48000 * 20 / 256; ++b) {
            for (int i = 0; i < 256; ++i) l[i] = r[i] = static_cast<float>(0.3 * std::sin(2 * kPi * f * (b * 256 + i) / kFs));
            float* c[2] = {l.data(), r.data()}; p.process(c, 2, 256);
        }
        CHECK(p.pivotHz() == doctest::Approx(f).epsilon(0.1));
    }
}
