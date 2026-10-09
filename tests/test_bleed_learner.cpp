// sw::BleedLearner (DY04 / CS02 "learn the bleed": a gate that opens for the wanted hits and not for the bleed of the other instruments)
#include "doctest.h"
#include "sw/bleed_learner.hpp"
#include "drums.hpp"
#include "tu.hpp"
using namespace tu;
using drumtest::drums;
namespace {
template <class L> void feed(L& l, const std::vector<float>& x) { for (size_t off = 0; off < x.size(); off += 256) l.process(x.data() + off, static_cast<int>(std::min<size_t>(256, x.size() - off))); }
}  // namespace

TEST_CASE("BleedLearner: the threshold goes between the hits and the bleed, the key filters fit the hits' band") {
    sw::BleedLearner l; l.prepare(kFs); l.start(30.0);
    feed(l, drums(12.0, -8.0, -34.0));
    CHECK(l.onsets() >= 20);
    const auto r = l.finish();
    REQUIRE(r.ok);
    INFO("threshold " << r.thresholdDb << " dB, HPF " << r.hpfHz << " Hz, LPF " << r.lpfHz << " Hz; hits " << r.targetCount << ", bleed " << r.bleedCount);
    CHECK(r.thresholdDb > -34.0 + 3.0); CHECK(r.thresholdDb < -8.0 - 3.0);
    NEAR(r.thresholdDb, -21.0, 6.0);
    CHECK(r.targetCount >= 8); CHECK(r.bleedCount >= 15);
    CHECK(r.hpfHz < 2200.0); CHECK(r.hpfHz >= 20.0);
    CHECK(r.lpfHz > 3500.0); CHECK(r.lpfHz < 12000.0);   // the bleed is above: the filter closes between the hits and it
    CHECK(!l.learning());
}

TEST_CASE("BleedLearner: low bleed (a kick in the snare mic) puts the HPF between the two") {
    sw::BleedLearner l; l.prepare(kFs); l.start(30.0);
    feed(l, drums(12.0, -8.0, -30.0, 3000.0, 120.0, false));
    const auto r = l.finish();
    REQUIRE(r.ok);
    CHECK(r.thresholdDb > -30.0 + 3.0); CHECK(r.thresholdDb < -8.0 - 3.0);
    CHECK(r.hpfHz > 250.0); CHECK(r.hpfHz < 2500.0);
    CHECK(r.lpfHz > 3500.0);
}

TEST_CASE("BleedLearner: nothing to separate (hits of one kind) or nothing heard is not a result; stopping early and the time limit") {
    { sw::BleedLearner l; l.prepare(kFs); l.start(30.0); feed(l, std::vector<float>(static_cast<size_t>(4 * kFs), 0.0f)); CHECK(!l.finish().ok); }
    { sw::BleedLearner l; l.prepare(kFs); l.start(30.0); feed(l, drums(10.0, -10.0, -10.0, 2500.0, 2500.0, false)); CHECK(!l.finish().ok); }   // the same kind at the same level
    { sw::BleedLearner l; l.prepare(kFs); l.start(5.0); feed(l, drums(8.0, -8.0, -34.0)); CHECK(!l.learning()); CHECK(l.progress() == doctest::Approx(1.0)); CHECK(l.finish().ok); }   // after 5 s it has stopped by itself
    { sw::BleedLearner l; l.prepare(kFs); CHECK(!l.learning()); CHECK(!l.finish().ok); }   // never started
}

TEST_CASE("BleedLearner: the same result whatever the block size") {
    const auto x = drums(10.0, -8.0, -34.0);
    sw::BleedLearner a, b; a.prepare(kFs); b.prepare(kFs); a.start(30.0); b.start(30.0);
    feed(a, x);
    for (size_t off = 0; off < x.size();) { const int m = static_cast<int>(std::min<size_t>(off % 7 + 1, x.size() - off)); b.process(x.data() + off, m); off += static_cast<size_t>(m); }
    const auto ra = a.finish(), rb = b.finish();
    CHECK(ra.ok == rb.ok); CHECK(ra.thresholdDb == rb.thresholdDb); CHECK(ra.hpfHz == rb.hpfHz); CHECK(ra.lpfHz == rb.lpfHz); CHECK(ra.targetCount == rb.targetCount);
}
