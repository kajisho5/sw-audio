// sw::BitDepthProbe (MS07 "Truncation check"): the coarsest grid (8 / 12 / 16 / 20 / 24 bit) the input's samples lie on, or float when they lie on none
#include "doctest.h"
#include "sw/bit_depth_probe.hpp"
#include "tu.hpp"
using namespace tu;
namespace {
// noise rounded to a grid of `bits` bits: n / 2^(bits - 1)
std::vector<float> onGrid(int bits, double seconds, double dbfs = -12.0, unsigned seed = 1) {
    auto x = noise(dbfs, seconds, seed); const double q = std::ldexp(1.0, bits - 1);
    for (auto& v : x) v = static_cast<float>(std::round(static_cast<double>(v) * q) / q);
    return x;
}
void feed(sw::BitDepthProbe& p, const std::vector<float>& x) { for (float v : x) p.add(v, v); }
}  // namespace

TEST_CASE("BitDepthProbe: a signal on an n-bit grid is found at n bits (8, 12, 16, 20, 24), whatever its level") {
    for (int bits : {8, 12, 16, 20, 24}) for (double db : {-3.0, -30.0}) {
        sw::BitDepthProbe p; p.prepare(kFs); p.start(); feed(p, onGrid(bits, 6.0, db));
        INFO(bits << " bit at " << db << " dBFS");
        CHECK(p.done()); CHECK(p.bits() == bits);
    }
}

TEST_CASE("BitDepthProbe: a float signal (or one that was scaled after being quantized) is on no grid; silence and the time limit give nothing; the numbers stay in range") {
    { sw::BitDepthProbe p; p.prepare(kFs); p.start(); feed(p, noise(-12.0, 6.0, 4)); CHECK(p.done()); CHECK(p.bits() == sw::BitDepthProbe::kFloat); }
    { sw::BitDepthProbe p; p.prepare(kFs); p.start(); auto x = onGrid(16, 6.0); for (auto& v : x) v = static_cast<float>(v * 0.7071); feed(p, x); CHECK(p.bits() == sw::BitDepthProbe::kFloat); }   // a 16 bit signal after a gain: the grid is gone
    { sw::BitDepthProbe p; p.prepare(kFs); p.start(); feed(p, std::vector<float>(static_cast<size_t>(10 * kFs), 0.0f)); CHECK(p.listening()); CHECK(!p.done()); CHECK(p.progress() == 0.0); CHECK(p.bits() == 0); }   // zeros are not signal
    { sw::BitDepthProbe p; p.prepare(kFs); p.start(); feed(p, std::vector<float>(static_cast<size_t>(65 * kFs), 0.0f)); CHECK(!p.listening()); CHECK(!p.done()); }   // gave up after 60 s
    { sw::BitDepthProbe p; p.prepare(kFs); p.start(); std::vector<float> x(static_cast<size_t>(6 * kFs)); for (size_t i = 0; i < x.size(); ++i) x[i] = (i % 3 == 0) ? 1e-40f : (i % 3 == 1 ? -3e-39f : 0.5f); feed(p, x); CHECK(p.done()); }   // denormals and a plain value do not break it
    { sw::BitDepthProbe p; p.prepare(kFs); p.start(); feed(p, onGrid(16, 2.0)); CHECK(p.listening()); CHECK(p.progress() == doctest::Approx(0.4).epsilon(0.02)); p.cancel(); CHECK(!p.listening()); CHECK(p.bits() == 0); }
}

TEST_CASE("BitDepthProbe: a few stray samples do not change the answer (99.9 % of the samples decide)") {
    auto x = onGrid(16, 6.0); for (size_t i = 0; i < x.size(); i += 4000) x[i] = static_cast<float>(x[i] * 0.7071);   // 0.025 % off the grid
    sw::BitDepthProbe p; p.prepare(kFs); p.start(); feed(p, x); CHECK(p.bits() == 16);
}
