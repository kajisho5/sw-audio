// RV08 Learn (EVO, class B; the learner is DY04's): it listens to the key as the gate hears it (the snare's bands), puts the Threshold between the snare and the bleed and switches Snare key on
#include "doctest.h"
#include "drums.hpp"
#include "rv08/rv08.hpp"
#include "tu.hpp"
using namespace tu;
using namespace sw::rv08;
using drumtest::drums;
namespace {
Processor make(std::vector<std::pair<int, double>> set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> runMono(Processor& p, std::vector<float> x, size_t block = 256) {
    std::vector<float> r = x;
    for (size_t off = 0; off < x.size(); off += block) { const int n = static_cast<int>(std::min(block, x.size() - off)); float* c[2] = {x.data() + off, r.data() + off}; p.process(c, 2, n); }
    return x;
}
double winDb(const std::vector<float>& y, double a, double b) { return rmsDb(y, static_cast<size_t>(a * kFs), static_cast<size_t>(b * kFs)); }
// the snare (a 2.5 kHz band, -3 dBFS peak) every 0.6 s and bleed in the same band (a tom: -14 / -17 dBFS) in between: only the level tells them apart
std::vector<float> kit(double seconds, unsigned seed = 1) { return drums(seconds, -3.0, -14.0, 2500.0, 2500.0, false, seed); }
}  // namespace

TEST_CASE("RV08 Learn: after listening, the Threshold sits between the snare and the bleed, Snare key is on, and only the snare opens the gate") {
    auto p = make();
    p.learn(); CHECK(p.learning());
    runMono(p, kit(12.0));
    CHECK(p.learnOnsets() >= 20);
    p.learn();
    CHECK(!p.learning()); CHECK(p.learnedOk());
    int id = -1; double v = 0; int n = 0; double thr = -1, snare = -1;
    while (p.takeParamWrite(id, v) == 7) { ++n; if (id == Threshold) thr = v; if (id == Snare) snare = v; }
    CHECK(n == 2);
    INFO("Threshold " << thr << " (" << thresholdDbfs(thr) << " dBFS), Snare key " << snare);
    CHECK(snare == 1.0);
    CHECK(thresholdDbfs(thr) > -30.0); CHECK(thresholdDbfs(thr) < -10.0);   // (the detector's level is the follower's, 10 dB or so under the peak)
    // learned: the reverb comes after the snare and not after the bleed (the wet signal is 0 while the gate is shut)
    auto q = p; const auto y = runMono(q, kit(6.0, 5));
    const double afterSnare = winDb(y, 0.5 + 0.6 * 3 + 0.03, 0.5 + 0.6 * 3 + 0.18), afterBleed = winDb(y, 0.2 + 0.6 * 3 + 0.02, 0.2 + 0.6 * 3 + 0.12);
    INFO("after the snare " << afterSnare << " dB, after the bleed " << afterBleed << " dB");
    CHECK(afterSnare > -60.0); CHECK(afterBleed < -90.0);
    // not learned (Threshold -30 dBFS, Snare key off): the bleed opens it too
    auto d = make(); const auto z = runMono(d, kit(6.0, 5));
    CHECK(winDb(z, 0.2 + 0.6 * 3 + 0.02, 0.2 + 0.6 * 3 + 0.12) > -60.0);
}

TEST_CASE("RV08 Learn: nothing to separate changes nothing and writes nothing; the time limit applies it by itself; the same values whatever the block size") {
    { auto p = make(); p.learn(); runMono(p, std::vector<float>(static_cast<size_t>(3 * kFs), 0.0f)); p.learn(); CHECK(!p.learnedOk()); int id; double v; CHECK(p.takeParamWrite(id, v) == 0); }
    { auto p = make(); p.learn(); runMono(p, drums(8.0, -10.0, -10.0, 2500.0, 2500.0, false)); p.learn(); CHECK(!p.learnedOk()); int id; double v; CHECK(p.takeParamWrite(id, v) == 0); }
    { auto p = make(); p.learn(); runMono(p, kit(31.0)); CHECK(!p.learning()); CHECK(p.learnedOk()); int id; double v; CHECK(p.takeParamWrite(id, v) == 7); }
    {
        const auto x = kit(10.0); auto a = make(), b = make(); a.learn(); b.learn(); runMono(a, x, 256); runMono(b, x, 37); a.learn(); b.learn();
        int ia, ib; double va, vb; int na = 0;
        while (a.takeParamWrite(ia, va) == 7) { REQUIRE(b.takeParamWrite(ib, vb) == 7); CHECK(ia == ib); CHECK(va == vb); ++na; }
        CHECK(na == 2);
    }
}

TEST_CASE("RV08 Learn: the key it listens to is the snare's bands, so a hi-hat above them is a small bleed and the learned Threshold keeps it out") {
    auto p = make(); p.learn(); runMono(p, drums(12.0, -3.0, -8.0)); p.learn();
    REQUIRE(p.learnedOk());
    auto q = p; const auto y = runMono(q, drums(6.0, -3.0, -8.0, 2500.0, 8000.0, true, 5));
    const double afterSnare = winDb(y, 0.5 + 0.6 * 3 + 0.03, 0.5 + 0.6 * 3 + 0.18), afterHat = winDb(y, 0.2 + 0.6 * 3 + 0.02, 0.2 + 0.6 * 3 + 0.12);
    INFO("after the snare " << afterSnare << " dB, after the hi-hat " << afterHat << " dB");
    CHECK(afterSnare > -60.0); CHECK(afterHat < -90.0);
    // a hi-hat at -8 dBFS is over the default Threshold (-30 dBFS) and opens the gate when nothing was learned
    auto d = make(); const auto z = runMono(d, drums(6.0, -3.0, -8.0, 2500.0, 8000.0, true, 5));
    CHECK(winDb(z, 0.2 + 0.6 * 3 + 0.02, 0.2 + 0.6 * 3 + 0.12) > -60.0);
}
