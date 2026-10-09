// DY10 Auto (EVO, class B): 10 s of playing -> the three crossovers (sw::CrossoverFinder); the core writes them to the host
#include "doctest.h"
#include "dy10/dy10.hpp"
#include "sw/crossover_finder.hpp"
#include "tu.hpp"
#include "xover_program.hpp"
using namespace tu;
using namespace sw::dy10;
namespace {
Processor make() { Processor p; p.prepare(kFs, 256); p.snapToTargets(); return p; }
void runMono(Processor& p, std::vector<float> x, size_t block = 256) {
    std::vector<float> r = x;
    for (size_t off = 0; off < x.size(); off += block) { const int n = static_cast<int>(std::min(block, x.size() - off)); float* c[2] = {x.data() + off, r.data() + off}; p.process(c, 2, n); }
}
// four clusters of sines, 1/12 octave apart, random phases: 90-180 Hz, 500-1000 Hz, 1.7-3.4 kHz, 5-9.5 kHz (the same K-weighted energy about each: 22 / 28 / 28 / 22 %), over a faint floor
std::vector<float> program(double seconds, unsigned seed = 1) { return xoverprog::clusters({{90, 180, 0.22}, {500, 1000, 0.28}, {1700, 3400, 0.28}, {5000, 9500, 0.22}}, seconds, seed); }
}  // namespace

TEST_CASE("DY10 Auto: after 10 s of playing the three crossovers are written (to the core at once, and to the host), an octave apart") {
    auto p = make();
    p.learn(); CHECK(p.learning());
    runMono(p, program(6.0)); CHECK(p.learning()); CHECK(p.learnProgress() > 0.4); CHECK(p.learnProgress() < 0.8);
    runMono(p, program(6.0, 2));
    CHECK(!p.learning()); CHECK(p.learnedOk());
    int id = -1; double v = 0; double x[3] = {0, 0, 0}; int n = 0;
    while (p.takeParamWrite(id, v) == 7) { REQUIRE(n < 3); CHECK(id == X1 + n); x[n++] = v; }
    CHECK(n == 3);
    INFO("crossovers " << x[0] << " " << x[1] << " " << x[2]);
    CHECK(x[1] >= 2.0 * x[0] - 1e-6); CHECK(x[2] >= 2.0 * x[1] - 1e-6); CHECK(x[0] >= 20.0); CHECK(x[2] <= 20000.0);
    for (int i = 0; i < 3; ++i) CHECK(p.crossoverHz(i) == doctest::Approx(x[i]).epsilon(1e-6));
    CHECK(x[0] > 180.0 * 0.9); CHECK(x[0] < 500.0 * 1.1); CHECK(x[2] > 3400.0 * 0.9);
}

TEST_CASE("DY10 Auto: pressing again while it listens cancels (nothing is written); silence is not playing; the same values whatever the block size") {
    { auto p = make(); p.learn(); runMono(p, program(4.0)); p.learn(); CHECK(!p.learning()); CHECK(!p.learnedOk()); int id; double v; CHECK(p.takeParamWrite(id, v) == 0); }
    { auto p = make(); p.learn(); runMono(p, std::vector<float>(static_cast<size_t>(20 * kFs), 0.0f)); CHECK(p.learning()); CHECK(p.learnProgress() == 0.0); }
    {
        const auto x = program(12.0, 3); auto a = make(), b = make(); a.learn(); b.learn(); runMono(a, x, 256); runMono(b, x, 37);
        int ia, ib; double va, vb; int na = 0;
        while (a.takeParamWrite(ia, va) == 7) { REQUIRE(b.takeParamWrite(ib, vb) == 7); CHECK(ia == ib); CHECK(va == vb); ++na; }
        CHECK(na == 3);
    }
}
