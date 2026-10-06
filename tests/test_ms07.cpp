#include "doctest.h"
#include "ms07/ms07.hpp"
#include <cmath>
#include <vector>
using namespace sw;
using namespace sw::ms07;
namespace {
const double kPi = 3.14159265358979323846, kFs = 48000.0;
std::vector<float> run(Processor& p, std::vector<float> l) { std::vector<float> r = l; for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } return l; }
std::vector<float> lowSine(int n) { std::vector<float> x(static_cast<size_t>(n)); for (int i = 0; i < n; ++i) x[static_cast<size_t>(i)] = static_cast<float>(0.001 * std::sin(2 * kPi * 440.0 * i / kFs)); return x; }
double bandNoiseDb(const std::vector<double>& r, double f0, double f1) {  // mean power per 10 Hz bin (Goertzel)
    double sum = 0; int bins = 0;
    for (double f = f0; f <= f1; f += 10.0, ++bins) {
        const double w = 2 * kPi * f / kFs, cw = 2 * std::cos(w); double s1 = 0, s2 = 0;
        for (double v : r) { const double s0 = v + cw * s1 - s2; s2 = s1; s1 = s0; }
        sum += s1 * s1 + s2 * s2 - cw * s1 * s2;
    }
    return 10 * std::log10(sum / bins);
}
std::vector<double> residual(Processor& p, int n) {
    const auto x = lowSine(n); const auto y = run(p, x);
    std::vector<double> r(static_cast<size_t>(n)); for (size_t i = 0; i < r.size(); ++i) r[i] = static_cast<double>(y[i]) - x[i]; return r;
}
}

TEST_CASE("MS07 parameter table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Bits].steps == std::vector<double>{16, 20, 24}); CHECK(s[Bits].def == 16);
    CHECK(s[Shape].labels == std::vector<std::string>{"Off", "Light", "Mid", "Strong", "Ultra"}); CHECK(s[Shape].def == 2);
    CHECK(s[Output].min == -10); CHECK(s[Blank].labels == std::vector<std::string>{"Auto blank", "Always"}); CHECK(s[Blank].def == 0);
}
TEST_CASE("16-bit output lands exactly on 16-bit steps") {
    Processor p; p.prepare(kFs, 256);
    for (float v : run(p, lowSine(4096))) { const double q = v * 32768.0; REQUIRE(q == std::round(q)); }
}
TEST_CASE("flat TPDF at 16 bit: total error about -96.3 dBFS RMS") {
    Processor p; p.setParam(Shape, 0); p.setParam(Blank, 1); p.prepare(kFs, 256);
    const auto r = residual(p, 48000);
    double s = 0; for (double v : r) s += v * v;
    CHECK(10 * std::log10(s / r.size()) == doctest::Approx(-96.3).epsilon(0.01));
}
TEST_CASE("noise shaping moves noise out of the 1-4 kHz region (Ultra at least 15 dB below flat)") {
    Processor flat; flat.setParam(Shape, 0); flat.setParam(Blank, 1); flat.prepare(kFs, 256);
    Processor ultra; ultra.setParam(Shape, 4); ultra.setParam(Blank, 1); ultra.prepare(kFs, 256);
    const double f = bandNoiseDb(residual(flat, 24000), 1000, 4000), u = bandNoiseDb(residual(ultra, 24000), 1000, 4000);
    CHECK(u < f - 15.0);
    for (double v : residual(ultra, 24000)) REQUIRE(std::abs(v) < 0.01);  // stays bounded
}
TEST_CASE("Auto blank: true digital silence comes out as exact silence") {
    Processor p; p.prepare(kFs, 256);
    std::vector<float> x(8192, 0.0f);
    for (int i = 6000; i < 8192; ++i) x[static_cast<size_t>(i)] = static_cast<float>(0.001 * std::sin(i * 0.05));
    const auto y = run(p, x);
    for (int i = 1100; i < 6000; ++i) REQUIRE(y[static_cast<size_t>(i)] == 0.0f);
    bool alive = false; for (int i = 6200; i < 8192; ++i) if (y[static_cast<size_t>(i)] != 0.0f) alive = true;
    CHECK(alive);
}
TEST_CASE("Output gain is applied before quantization") {
    Processor p; p.setParam(Output, 6.0206); p.setParam(Bits, 24); p.prepare(kFs, 256);
    const auto y = run(p, std::vector<float>(1024, 0.25f));
    CHECK(y.back() == doctest::Approx(0.5).epsilon(1e-5));
}
