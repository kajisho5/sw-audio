// The audio thread does not allocate - the learning buttons too: the screen's call reaches the core on the audio thread (guiCall), and the result is applied there (tests/alloc_guard.hpp)
#include "doctest.h"
#include "alloc_guard.hpp"
#include "cs02/cs02.hpp"
#include "cs03/cs03.hpp"
#include "drums.hpp"
#include "dy04/dy04.hpp"
#include "dy10/dy10.hpp"
#include "ms07/ms07.hpp"
#include "rv08/rv08.hpp"
#include "tu.hpp"
#include "xover_program.hpp"
#include <atomic>
#include <thread>
using namespace tu;
namespace {
// blocks of 256 prepared before the guard is on; the guard covers learn(), the processing (stereo, the same samples on both channels) and the write queue
struct Run { std::vector<std::vector<float>> l, r; };
Run blocks(const std::vector<float>& x) {
    Run b; for (size_t off = 0; off < x.size(); off += 256) { std::vector<float> c(256, 0.0f); for (size_t i = 0; i < 256 && off + i < x.size(); ++i) c[i] = x[off + i]; b.l.push_back(c); b.r.push_back(c); }
    return b;
}
template <class P> long learnCycle(P& p, Run& b, bool pressAgain) {
    allocguard::Scope g;
    p.learn();
    for (size_t k = 0; k < b.l.size(); ++k) { float* c[2] = {b.l[k].data(), b.r[k].data()}; p.process(c, 2, 256); }
    if (pressAgain) p.learn();
    int id; double v; while (p.takeParamWrite(id, v) == 7) {}
    return g.n();
}
}  // namespace

TEST_CASE("the audio thread does not allocate while a Learn runs and is applied: DY04, CS02, RV08") {
    auto run = blocks(drumtest::drums(12.0, -8.0, -34.0));
    { sw::dy04::Processor p; p.prepare(kFs, 256); p.snapToTargets(); CHECK(learnCycle(p, run, true) == 0); CHECK(p.learnedOk()); }
    { sw::cs02::Processor p; p.prepare(kFs, 256); p.snapToTargets(); CHECK(learnCycle(p, run, true) == 0); CHECK(p.learnedOk()); }
    { auto k = blocks(drumtest::drums(12.0, -3.0, -14.0, 2500.0, 2500.0, false)); sw::rv08::Processor p; p.prepare(kFs, 256); p.snapToTargets(); CHECK(learnCycle(p, k, true) == 0); CHECK(p.learnedOk()); }
    { auto t = blocks(drumtest::drums(31.0, -8.0, -34.0)); sw::dy04::Processor p; p.prepare(kFs, 256); p.snapToTargets(); CHECK(learnCycle(p, t, false) == 0); CHECK(p.learnedOk()); }   // the time limit applies it by itself
}

TEST_CASE("the audio thread does not allocate while a Set input, an Auto, a Truncation check runs and is applied: CS03, DY10, MS07") {
    { auto x = noise(-38.0, 6.0, 3); auto run = blocks(x); sw::cs03::Processor p; p.prepare(kFs, 256); p.snapToTargets(); CHECK(learnCycle(p, run, false) == 0); CHECK(p.learnedOk()); }
    { auto run = blocks(xoverprog::clusters({{90, 180, 0.22}, {500, 1000, 0.28}, {1700, 3400, 0.28}, {5000, 9500, 0.22}}, 12.0)); sw::dy10::Processor p; p.prepare(kFs, 256); p.snapToTargets(); CHECK(learnCycle(p, run, false) == 0); CHECK(p.learnedOk()); }
    { auto x = noise(-12.0, 6.0, 4); const double q = 32768.0; for (auto& v : x) v = static_cast<float>(std::round(static_cast<double>(v) * q) / q); auto run = blocks(x);
      sw::ms07::Processor p; p.prepare(kFs, 256); p.snapToTargets();
      { allocguard::Scope g; p.check(); for (size_t k = 0; k < run.l.size(); ++k) { float* c[2] = {run.l[k].data(), run.r[k].data()}; p.process(c, 2, 256); } CHECK(g.n() == 0); }
      CHECK(p.checkedBits() == 16); }
}

TEST_CASE("the allocation guard counts what happens while it is on, and only on its own thread") {
    long n;
    { allocguard::Scope g; std::vector<int>* v = new std::vector<int>(100); delete v; n = g.n(); }
    CHECK(n >= 2);
    { allocguard::Scope g; int x = 3; (void)x; n = g.n(); }
    CHECK(n == 0);
    std::atomic<bool> go{false}, done{false};
    std::thread t([&] { while (!go) std::this_thread::yield(); std::vector<int> w(100); (void)w; done = true; });
    long other;
    { allocguard::Scope g; go = true; while (!done) std::this_thread::yield(); other = g.n(); }
    t.join();
    CHECK(other == 0);   // another thread's allocations are not counted (the flag is thread-local)
}
