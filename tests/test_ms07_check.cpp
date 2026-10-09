// MS07 Truncation check: the button listens to the input (5 s of signal) and reports the grid it lies on; the sound is not touched
#include "doctest.h"
#include "ms07/ms07.hpp"
#include "tu.hpp"
using namespace tu;
using namespace sw::ms07;
namespace {
Processor make(std::vector<std::pair<int, double>> set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> runMono(Processor& p, std::vector<float> x, size_t block = 256) {
    std::vector<float> r = x; std::vector<float> out(x.size());
    for (size_t off = 0; off < x.size(); off += block) { const int n = static_cast<int>(std::min(block, x.size() - off)); float* c[2] = {x.data() + off, r.data() + off}; p.process(c, 2, n); for (int i = 0; i < n; ++i) out[off + static_cast<size_t>(i)] = c[0][i]; }
    return out;
}
std::vector<float> onGrid(int bits, double seconds, unsigned seed = 1) { auto x = noise(-12.0, seconds, seed); const double q = std::ldexp(1.0, bits - 1); for (auto& v : x) v = static_cast<float>(std::round(static_cast<double>(v) * q) / q); return x; }
}  // namespace

TEST_CASE("MS07 Truncation check: a 16 bit input is found at 16 bit, a float input as float; it ends by itself after 5 s of signal") {
    { auto p = make(); p.check(); CHECK(p.checking()); runMono(p, onGrid(16, 4.0)); CHECK(p.checking()); CHECK(p.checkProgress() > 0.6); CHECK(p.checkProgress() < 0.9); CHECK(p.checkedBits() == 0);
      runMono(p, onGrid(16, 4.0)); CHECK(!p.checking()); CHECK(p.checkedBits() == 16); }
    { auto p = make(); p.check(); runMono(p, noise(-12.0, 6.0, 4)); CHECK(p.checkedBits() == sw::BitDepthProbe::kFloat); }
    { auto p = make(); p.check(); runMono(p, onGrid(24, 6.0)); CHECK(p.checkedBits() == 24); }
}

TEST_CASE("MS07 Truncation check: pressing while it listens cancels; the output is the same with and without it; the same answer whatever the block size") {
    { auto p = make(); p.check(); runMono(p, onGrid(16, 2.0)); p.check(); CHECK(!p.checking()); CHECK(p.checkedBits() == 0); }
    {   // the check only listens: the same input gives the same output (the dither is random, so compare with the same seed: a fresh core each)
        const auto x = onGrid(16, 1.0); auto a = make({{Shape, 0.0}}), b = make({{Shape, 0.0}}); b.check();
        const auto ya = runMono(a, x), yb = runMono(b, x); CHECK(ya == yb);
    }
    { const auto x = onGrid(20, 6.0); auto a = make(), b = make(); a.check(); b.check(); runMono(a, x, 256); runMono(b, x, 37); CHECK(a.checkedBits() == 20); CHECK(b.checkedBits() == 20); }
}
