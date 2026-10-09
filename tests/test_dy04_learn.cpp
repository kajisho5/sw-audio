// DY04 Learn (EVO, class B): it listens to the key, measures the onsets, and puts the Threshold between the wanted hits and the bleed and the key filter frequencies at the edges of the hits' band
#include "doctest.h"
#include "drums.hpp"
#include "dy04/dy04.hpp"
#include "tu.hpp"
using namespace tu;
using namespace sw::dy04;
using drumtest::drums;
namespace {
Processor make() { Processor p; p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> runMono(Processor& p, std::vector<float> x, size_t block = 256) {
    std::vector<float> r = x;
    for (size_t off = 0; off < x.size(); off += block) { const int n = static_cast<int>(std::min(block, x.size() - off)); float* c[2] = {x.data() + off, r.data() + off}; p.process(c, 2, n); }
    return x;
}
// the peak of the output around each kind of hit: hits at 0.5 + 0.6 k s, bleed at 0.2 + 0.6 k and 0.35 + 0.6 k
double peakDbAt(const std::vector<float>& y, double t0, double span = 0.1) { double pk = 0; for (size_t i = static_cast<size_t>(t0 * kFs); i < static_cast<size_t>((t0 + span) * kFs) && i < y.size(); ++i) pk = std::max(pk, static_cast<double>(std::fabs(y[i]))); return 20.0 * std::log10(pk + 1e-9); }
}  // namespace

TEST_CASE("DY04 Learn: after listening to hits with bleed, the Threshold sits between them and the gate keeps the hits and shuts the bleed") {
    auto p = make();
    const auto x = drums(12.0, -8.0, -34.0);
    p.learn(); CHECK(p.learning());
    runMono(p, x);
    CHECK(p.learnOnsets() >= 20);
    p.learn();               // pressed again: it stops and applies what it heard
    CHECK(!p.learning()); CHECK(p.learnedOk());
    int id = -1; double v = 0; int n = 0; double thr = 0, hpf = 0, lpf = 0;
    while (p.takeParamWrite(id, v) == 7) { ++n; if (id == Threshold) thr = v; if (id == KeyHpfHz) hpf = v; if (id == KeyLpfHz) lpf = v; }
    CHECK(n == 3);
    INFO("threshold " << thr << ", HPF " << hpf << ", LPF " << lpf);
    CHECK(thr > -34.0 + 3.0); CHECK(thr < -8.0 - 3.0);
    CHECK(hpf >= 20.0); CHECK(hpf < 2200.0); CHECK(lpf > 3500.0); CHECK(lpf <= 20000.0);
    // the core uses them at once: a fresh run of the same drums through the gate (Range -40)
    const auto y = runMono(p, drums(6.0, -8.0, -34.0, 2500.0, 8000.0, true, 5));
    const double hit = peakDbAt(y, 0.5 + 0.6 * 3), bleed = peakDbAt(y, 0.2 + 0.6 * 3, 0.08);
    CHECK(hit > -9.5);                      // the hit comes through (-8 dBFS peak)
    CHECK(bleed < -34.0 - 20.0);            // the bleed is down by Range (-40) and some
    // the same drums with the gate open (the default Threshold) leave the bleed where it was
    auto open = make(); const auto z = runMono(open, drums(6.0, -8.0, -34.0, 2500.0, 8000.0, true, 5));
    CHECK(peakDbAt(z, 0.2 + 0.6 * 3, 0.08) > -36.0);
}

TEST_CASE("DY04 Learn: nothing to separate changes nothing and writes nothing; the time limit applies it by itself; the screen's call is queued to the audio thread by the plug-in") {
    { auto p = make(); p.learn(); runMono(p, std::vector<float>(static_cast<size_t>(3 * kFs), 0.0f)); p.learn();
      CHECK(!p.learnedOk()); int id; double v; CHECK(p.takeParamWrite(id, v) == 0); }
    { auto p = make(); p.learn(); runMono(p, drums(8.0, -10.0, -10.0, 2500.0, 2500.0, false)); p.learn(); CHECK(!p.learnedOk()); int id; double v; CHECK(p.takeParamWrite(id, v) == 0); }
    {   // 31 s of drums: the 30 s limit stops it and applies the result without a second press
        auto p = make(); p.learn(); runMono(p, drums(31.0, -8.0, -34.0));
        CHECK(!p.learning()); CHECK(p.learnedOk()); int id; double v; CHECK(p.takeParamWrite(id, v) == 7);
    }
}

TEST_CASE("DY04 Learn: the same Threshold and key frequencies whatever the block size") {
    const auto x = drums(10.0, -8.0, -34.0);
    auto a = make(), b = make(); a.learn(); b.learn();
    runMono(a, x, 256); runMono(b, x, 37);
    a.learn(); b.learn();
    int ia, ib; double va, vb; double ta[3] = {0, 0, 0}, tb[3] = {0, 0, 0}; int na = 0, nb = 0;
    while (a.takeParamWrite(ia, va) == 7 && na < 3) ta[na++] = va;
    while (b.takeParamWrite(ib, vb) == 7 && nb < 3) tb[nb++] = vb;
    CHECK(na == 3); CHECK(nb == 3);
    for (int k = 0; k < 3; ++k) CHECK(ta[k] == tb[k]);
}
