// EQ02 Linear: the two kernels (one per path) are designed on a background thread (Processor::useWorker, which the plug-in layer switches on): the same kernels as the audio thread would design,
// nothing designed in process(), nothing allocated there
#include "doctest.h"
#include "alloc_guard.hpp"
#include "eq02/eq02.hpp"
#include "tu.hpp"
#include <thread>
using namespace tu;
using namespace sw::eq02;
namespace {
int band(int n, int field) { return (n - 1) * kPerBand + field; }
void setBands(Processor& p, double gainDb, double freq) {
    p.setParam(band(1, On), 1); p.setParam(band(1, Gain), gainDb); p.setParam(band(1, Freq), freq); p.setParam(band(1, Place), 1);    // mid only (Mid/side on)
    p.setParam(band(2, On), 1); p.setParam(band(2, Type), 1); p.setParam(band(2, Gain), -gainDb / 2); p.setParam(band(2, Place), 2);  // side only
    p.setParam(band(3, On), 1); p.setParam(band(3, Gain), gainDb / 3); p.setParam(band(3, Freq), 6000.0);                               // both
}
// 256-sample blocks of l / r (a different noise on each side); `after` runs after each block (e.g. waitKernel)
template <class F> std::pair<std::vector<float>, std::vector<float>> through(Processor& p, std::vector<float> l, std::vector<float> r, F after) {
    for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); after(); }
    return {l, r};
}
}  // namespace

TEST_CASE("EQ02 worker: after a change the worker thread's kernels are the ones the audio thread designs, and they are designed on another thread") {
    Processor a, b; b.useWorker(true);
    for (Processor* p : {&a, &b}) { p->setParam(PhaseMode, Linear); p->setParam(Ms, 1); p->prepare(kFs, 256); p->snapToTargets(); }
    CHECK(!a.workerRunning()); CHECK(b.workerRunning());
    const auto l = noise(-20.0, 4.0, 5), r = noise(-20.0, 4.0, 6);
    const size_t half = l.size() / 4;
    auto cut = [&](const std::vector<float>& x, bool head) { return head ? std::vector<float>(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(half)) : std::vector<float>(x.begin() + static_cast<std::ptrdiff_t>(half), x.end()); };
    through(a, cut(l, true), cut(r, true), [] {}); through(b, cut(l, true), cut(r, true), [&] { b.waitKernel(); });
    setBands(a, 9.0, 3000.0); setBands(b, 9.0, 3000.0);
    const auto ya = through(a, cut(l, false), cut(r, false), [] {}), yb = through(b, cut(l, false), cut(r, false), [&] { b.waitKernel(); });
    double worst = 0;
    for (size_t i = ya.first.size() - 24000; i < ya.first.size(); ++i) worst = std::max({worst, static_cast<double>(std::abs(ya.first[i] - yb.first[i])), static_cast<double>(std::abs(ya.second[i] - yb.second[i]))});
    INFO("largest difference in the last 0.5 s " << worst);
    CHECK(worst < 1e-6);
    CHECK(a.kernelsApplied() >= 1); CHECK(b.kernelsApplied() >= 2);
    CHECK(a.lastDesignThread() == std::this_thread::get_id());
    CHECK(b.lastDesignThread() != std::this_thread::get_id());
    CHECK(rmsDb(yb.first, yb.first.size() - 24000, yb.first.size()) != doctest::Approx(rmsDb(l, l.size() - 24000, l.size())).epsilon(0.001));   // (the change did something)
}

TEST_CASE("EQ02 worker: only Linear has kernels and a thread; process() allocates nothing and designs nothing while knobs move; a copy designs for itself") {
    for (const int mode : {ZeroLatency, Natural}) { Processor p; p.useWorker(true); p.setParam(PhaseMode, mode); p.prepare(kFs, 256); CHECK(!p.workerRunning()); }
    Processor p; p.useWorker(true); p.setParam(PhaseMode, Linear); p.prepare(kFs, 256); p.snapToTargets(); REQUIRE(p.workerRunning());
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

TEST_CASE("EQ02 worker: a state load (snapToTargets) wins over a design on its way; prepare again (another rate) and end while a design is running") {
    Processor a, b; b.useWorker(true);
    for (Processor* p : {&a, &b}) { p->setParam(PhaseMode, Linear); p->setParam(Ms, 1); p->prepare(kFs, 256); p->snapToTargets(); }
    const auto l = noise(-20.0, 1.0, 9), r = noise(-20.0, 1.0, 10);
    through(b, l, r, [] {});   // (nothing changed yet)
    setBands(b, 12.0, 2000.0); std::vector<float> tmp(256, 0.1f), t2 = tmp; float* c[2] = {tmp.data(), t2.data()};
    for (int k = 0; k < 4; ++k) b.process(c, 2, 256);   // (a design may be on its way ...)
    setBands(b, -6.0, 800.0); setBands(a, -6.0, 800.0);
    b.snapToTargets(); a.snapToTargets();               // ... and the loaded state takes its place
    const auto ya = through(a, l, r, [] {}), yb = through(b, l, r, [&] { b.waitKernel(); });
    double worst = 0; for (size_t i = ya.first.size() - 12000; i < ya.first.size(); ++i) worst = std::max({worst, static_cast<double>(std::abs(ya.first[i] - yb.first[i])), static_cast<double>(std::abs(ya.second[i] - yb.second[i]))});
    CHECK(worst < 1e-6);
    b.prepare(96000.0, 256); b.snapToTargets(); CHECK(b.workerRunning());
    for (int k = 0; k < 6; ++k) { setBands(b, k % 2 ? 5.0 : -5.0, 1000.0 + 100.0 * k); float* d[2] = {tmp.data(), t2.data()}; b.process(d, 2, 256); }
    // (destroyed with a design possibly running: the destructor joins the thread)
}

TEST_CASE("EQ02 reset(): the host stopped - the audio is forgotten in every phase mode (filters, detectors, convolver), the settings and the kernels stay, nothing is allocated") {
    for (const int mode : {ZeroLatency, Natural, Linear}) {
        Processor p; p.setParam(PhaseMode, mode); setBands(p, 9.0, 3000.0); p.setParam(band(4, On), 1); p.setParam(band(4, DynRange), -6.0); p.setParam(band(4, Freq), 1200.0);
        p.prepare(kFs, 256); p.snapToTargets();
        Processor fresh = p;
        const auto l = noise(-12.0, 2.0, 3), r = noise(-12.0, 2.0, 4);
        through(p, l, r, [] {});
        { allocguard::Scope g; p.reset(); CHECK(g.n() == 0); }
        const auto sil = through(p, std::vector<float>(9600, 0.0f), std::vector<float>(9600, 0.0f), [] {});
        double worst = 0; for (size_t i = 0; i < sil.first.size(); ++i) worst = std::max({worst, static_cast<double>(std::abs(sil.first[i])), static_cast<double>(std::abs(sil.second[i]))});
        INFO("mode " << mode << ": loudest sample after reset " << worst);
        CHECK(worst < 1e-6);
        // and it still does what it did: the same input gives what a fresh one gives
        const auto a = through(p, l, r, [] {}), b = through(fresh, l, r, [] {});
        double diff = 0; for (size_t i = a.first.size() / 2; i < a.first.size(); ++i) diff = std::max(diff, static_cast<double>(std::abs(a.first[i] - b.first[i])));
        CHECK(diff < 1e-4);
    }
}
