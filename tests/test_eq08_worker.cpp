// EQ08 Linear / Mixed: the kernel is designed on a background thread (Processor::useWorker, which the plug-in layer switches on): the same kernel as the audio thread would design, nothing designed in process(), nothing allocated there
#include "doctest.h"
#include "alloc_guard.hpp"
#include "eq08/eq08.hpp"
#include "tu.hpp"
#include <thread>
using namespace tu;
using namespace sw::eq08;
namespace {
int band(int n, int field) { return (n - 1) * kPerBand + field; }
void setBands(Processor& p, double gainDb, double freq) { p.setParam(band(1, On), 1); p.setParam(band(1, Gain), gainDb); p.setParam(band(1, Freq), freq); p.setParam(band(2, On), 1); p.setParam(band(2, Type), 1); p.setParam(band(2, Gain), -gainDb / 2); }
// 256-sample blocks of `x` (both channels the same); `after` runs after each block (e.g. waitKernel)
template <class F> std::vector<float> through(Processor& p, std::vector<float> x, F after) {
    std::vector<float> r = x;
    for (size_t off = 0; off < x.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, x.size() - off)); float* c[2] = {x.data() + off, r.data() + off}; p.process(c, 2, n); after(); }
    return x;
}
}  // namespace

TEST_CASE("EQ08 worker: after a change the worker thread's kernel is the one the audio thread designs, and it is designed on another thread") {
    for (const int mode : {Linear, Mixed}) {
        Processor a, b; b.useWorker(true);
        for (Processor* p : {&a, &b}) { p->setParam(Phase, mode); p->prepare(kFs, 256); p->snapToTargets(); }
        CHECK(!a.workerRunning()); CHECK(b.workerRunning());
        const auto x = noise(-20.0, 4.0, 5);
        const size_t half = x.size() / 4;
        std::vector<float> head(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(half)), tail(x.begin() + static_cast<std::ptrdiff_t>(half), x.end());
        through(a, head, [] {}); through(b, head, [&] { b.waitKernel(); });
        setBands(a, 9.0, 3000.0); setBands(b, 9.0, 3000.0);
        const auto ya = through(a, tail, [] {}), yb = through(b, tail, [&] { b.waitKernel(); });
        double worst = 0; for (size_t i = ya.size() - 24000; i < ya.size(); ++i) worst = std::max(worst, static_cast<double>(std::abs(ya[i] - yb[i])));
        INFO("mode " << mode << ": largest difference in the last 0.5 s " << worst);
        CHECK(worst < 1e-6);
        CHECK(a.kernelsApplied() >= 1); CHECK(b.kernelsApplied() >= 2);
        CHECK(a.lastDesignThread() == std::this_thread::get_id());
        CHECK(b.lastDesignThread() != std::this_thread::get_id());
        CHECK(rmsDb(yb, yb.size() - 24000, yb.size()) != doctest::Approx(rmsDb(x, x.size() - 24000, x.size())).epsilon(0.001));   // (the change did something)
    }
}

TEST_CASE("EQ08 worker: Minimum has no kernel and no thread; process() allocates nothing and designs nothing while knobs move; a copy designs for itself") {
    { Processor p; p.useWorker(true); p.setParam(Phase, Minimum); p.prepare(kFs, 256); CHECK(!p.workerRunning()); }
    Processor p; p.useWorker(true); p.prepare(kFs, 256); p.snapToTargets(); REQUIRE(p.workerRunning());
    auto x = noise(-20.0, 2.0, 7); std::vector<float> r = x;
    const int before = p.kernelsApplied();
    {   // the guard covers the audio thread only (the worker's allocations are not counted: they are not the audio thread's)
        allocguard::Scope g;
        for (size_t off = 0, k = 0; off + 256 <= x.size(); off += 256, ++k) {
            p.setParam(band(1, On), 1); p.setParam(band(1, Gain), (k % 2 ? 6.0 : -6.0)); p.setParam(band(1, Freq), 500.0 + 13.0 * static_cast<double>(k));
            float* c[2] = {x.data() + off, r.data() + off}; p.process(c, 2, 256);
            if (k % 8 == 7) p.waitKernel();
        }
        CHECK(g.n() == 0);
    }
    p.waitKernel();
    CHECK(p.kernelsApplied() > before + 2);
    CHECK(p.lastDesignThread() != std::this_thread::get_id());
    Processor c = p;   // a copy has no thread: it designs in process() until its own prepare()
    CHECK(!c.workerRunning());
    c.setParam(band(3, On), 1); c.setParam(band(3, Gain), 4.0);
    const int n0 = c.kernelsApplied(); std::vector<float> y(4800, 0.1f), z = y;
    for (size_t off = 0; off < y.size(); off += 256) { float* ch[2] = {y.data() + off, z.data() + off}; c.process(ch, 2, static_cast<int>(std::min<size_t>(256, y.size() - off))); }
    CHECK(c.kernelsApplied() > n0); CHECK(c.lastDesignThread() == std::this_thread::get_id());
}

TEST_CASE("EQ08 worker: a state load (snapToTargets) wins over a design on its way; prepare again (another rate) and end while a design is running") {
    Processor a, b; b.useWorker(true);
    for (Processor* p : {&a, &b}) { p->prepare(kFs, 256); p->snapToTargets(); }
    const auto x = noise(-20.0, 1.0, 9);
    through(b, x, [] {});   // (nothing changed yet)
    setBands(b, 12.0, 2000.0); std::vector<float> tmp(256, 0.1f), t2 = tmp; float* c[2] = {tmp.data(), t2.data()};
    for (int k = 0; k < 4; ++k) b.process(c, 2, 256);   // (a design may be on its way ...)
    setBands(b, -6.0, 800.0); setBands(a, -6.0, 800.0);
    b.snapToTargets(); a.snapToTargets();               // ... and the loaded state takes its place
    const auto ya = through(a, x, [] {}), yb = through(b, x, [&] { b.waitKernel(); });
    double worst = 0; for (size_t i = ya.size() - 12000; i < ya.size(); ++i) worst = std::max(worst, static_cast<double>(std::abs(ya[i] - yb[i])));
    CHECK(worst < 1e-6);
    b.prepare(96000.0, 256); b.snapToTargets(); CHECK(b.workerRunning());
    for (int k = 0; k < 6; ++k) { setBands(b, k % 2 ? 5.0 : -5.0, 1000.0 + 100.0 * k); float* d[2] = {tmp.data(), t2.data()}; b.process(d, 2, 256); }
    // (destroyed with a design possibly running: the destructor joins the thread)
}
