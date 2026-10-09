// CS02 Learn (EVO, class B; the learner is DY04's): it listens to the strip's input, measures the onsets, and puts the Gate threshold between the wanted hits and the bleed and the gate's key filters
// (internal values, not knobs) at the edges of the hits' band
#include "doctest.h"
#include "drums.hpp"
#include "cs02/cs02.hpp"
#include "tu.hpp"
using namespace tu;
using namespace sw::cs02;
using drumtest::drums;
namespace {
Processor make(std::vector<std::pair<int, double>> set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> runMono(Processor& p, std::vector<float> x, size_t block = 256) {
    std::vector<float> r = x;
    for (size_t off = 0; off < x.size(); off += block) { const int n = static_cast<int>(std::min(block, x.size() - off)); float* c[2] = {x.data() + off, r.data() + off}; p.process(c, 2, n); }
    return x;
}
double peakDbAt(const std::vector<float>& y, double t0, double span = 0.1) { double pk = 0; for (size_t i = static_cast<size_t>(t0 * kFs); i < static_cast<size_t>((t0 + span) * kFs) && i < y.size(); ++i) pk = std::max(pk, static_cast<double>(std::fabs(y[i]))); return 20.0 * std::log10(pk + 1e-9); }
}  // namespace

TEST_CASE("CS02 key filters: two internal values appended after Unit; at their defaults they do nothing") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(Unit + 1 == KeyHpf); CHECK(KeyHpf + 1 == KeyLpf); CHECK(KeyLpf + 1 == kNumParams);
    CHECK(std::string(s[KeyHpf].id) == "cs02.gate.keyhpf"); CHECK(s[KeyHpf].min == 20); CHECK(s[KeyHpf].max == 2000); CHECK(s[KeyHpf].def == 20);
    CHECK(std::string(s[KeyLpf].id) == "cs02.gate.keylpf"); CHECK(s[KeyLpf].min == 1000); CHECK(s[KeyLpf].max == 20000); CHECK(s[KeyLpf].def == 20000);
    // the defaults leave the gate as it was: the same output with the key filters set to their bypass values and left alone
    const auto x = noise(-30.0, 1.0, 3);
    auto a = make({{GateThresh, -10.0}, {GateRange, 20.0}}), b = make({{GateThresh, -10.0}, {GateRange, 20.0}, {KeyHpf, 20.0}, {KeyLpf, 20000.0}});
    const auto ya = runMono(a, x), yb = runMono(b, x);
    for (size_t i = 0; i < ya.size(); ++i) REQUIRE(ya[i] == yb[i]);
}

TEST_CASE("CS02 Learn: after listening to hits with bleed, the Gate sits between them and the strip keeps the hits and shuts the bleed") {
    auto p = make({{GateRange, 30.0}});
    const auto x = drums(12.0, -8.0, -34.0);
    p.learn(); CHECK(p.learning());
    runMono(p, x);
    CHECK(p.learnOnsets() >= 20);
    p.learn();   // pressed again: it stops and applies what it heard
    CHECK(!p.learning()); CHECK(p.learnedOk());
    int id = -1; double v = 0; int n = 0; double thr = 0, hpf = 0, lpf = 0;
    while (p.takeParamWrite(id, v) == 7) { ++n; if (id == GateThresh) thr = v; if (id == KeyHpf) hpf = v; if (id == KeyLpf) lpf = v; }
    CHECK(n == 3);
    INFO("Gate " << thr << " dB (0 dB = -18 dBFS), key HPF " << hpf << ", LPF " << lpf);
    CHECK(thr + (-18.0) > -34.0 + 3.0); CHECK(thr + (-18.0) < -8.0 - 3.0);   // in dBFS: between the bleed and the hits
    CHECK(thr >= -30.0); CHECK(thr <= 10.0);
    CHECK(hpf >= 20.0); CHECK(hpf < 2200.0); CHECK(lpf > 3500.0); CHECK(lpf <= 20000.0);
    // the core uses them at once: a fresh run of the same drums through the strip
    auto q = p;
    const auto y = runMono(q, drums(6.0, -8.0, -34.0, 2500.0, 8000.0, true, 5));
    const double hit = peakDbAt(y, 0.5 + 0.6 * 3), bleed = peakDbAt(y, 0.2 + 0.6 * 3, 0.08);
    CHECK(hit > -9.5);
    CHECK(bleed < -34.0 - 15.0);
    // without Learn (the Gate at its default, -30 dB = -48 dBFS) the bleed passes
    auto open = make({{GateRange, 30.0}}); const auto z = runMono(open, drums(6.0, -8.0, -34.0, 2500.0, 8000.0, true, 5));
    CHECK(peakDbAt(z, 0.2 + 0.6 * 3, 0.08) > -36.0);
}

TEST_CASE("CS02 Learn: nothing to separate changes nothing and writes nothing; the time limit applies it by itself") {
    { auto p = make(); p.learn(); runMono(p, std::vector<float>(static_cast<size_t>(3 * kFs), 0.0f)); p.learn();
      CHECK(!p.learnedOk()); int id; double v; CHECK(p.takeParamWrite(id, v) == 0); }
    { auto p = make(); p.learn(); runMono(p, drums(8.0, -10.0, -10.0, 2500.0, 2500.0, false)); p.learn(); CHECK(!p.learnedOk()); int id; double v; CHECK(p.takeParamWrite(id, v) == 0); }
    { auto p = make(); p.learn(); runMono(p, drums(31.0, -8.0, -34.0)); CHECK(!p.learning()); CHECK(p.learnedOk()); int id; double v; CHECK(p.takeParamWrite(id, v) == 7); }
}

TEST_CASE("CS02 Learn: the same values whatever the block size, and written back into a fresh strip (a saved project) they give the same sound") {
    const auto x = drums(10.0, -8.0, -34.0);
    auto a = make({{GateRange, 30.0}}), b = make({{GateRange, 30.0}}); a.learn(); b.learn();
    runMono(a, x, 256); runMono(b, x, 37);
    a.learn(); b.learn();
    auto fresh = make({{GateRange, 30.0}});
    int ia, ib; double va, vb; int na = 0;
    while (a.takeParamWrite(ia, va) == 7) { REQUIRE(b.takeParamWrite(ib, vb) == 7); CHECK(ia == ib); CHECK(va == vb); fresh.setParam(ia, va); ++na; }
    CHECK(na == 3);
    fresh.snapToTargets();
    const auto in = drums(6.0, -8.0, -34.0, 2500.0, 8000.0, true, 5);
    const auto ya = runMono(a, in), yf = runMono(fresh, in);
    double err = 0; for (size_t i = static_cast<size_t>(kFs); i < ya.size(); ++i) err = std::max(err, static_cast<double>(std::fabs(ya[i] - yf[i])));
    CHECK(err < 1e-5);
}
