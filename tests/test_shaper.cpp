#include "doctest.h"
#include "os_helpers.hpp"
#include "sw/shaper.hpp"
#include "tu.hpp"
using namespace sw;
using namespace tu;
namespace {
std::vector<float> run1(double g, double b, double db, double f = 1000, double sec = 2) {
    BiasShaper2x s; s.prepare(kFs); const auto x = sine(db, sec, f); std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = static_cast<float>(s.process(0, x[i], g, b));
    return y;
}
}
TEST_CASE("BiasShaper2x: small-signal gain is 1 for any drive and bias") {
    for (double g : {1.0, 4.0, 16.0}) for (double b : {0.0, 0.3, 0.6}) NEAR(rmsDb(run1(g, b, -60)) - -60.0, 0.0, 0.1);
}
TEST_CASE("BiasShaper2x: no bias = odd harmonics only, bias = even harmonics lead; the DC shift is removed") {
    const auto sym = run1(4.0, 0.0, -10), asym = run1(4.0, 0.3, -10);
    CHECK(harmDb(sym, 1000, 2) < -90.0); CHECK(harmDb(sym, 1000, 3) > -40.0);
    CHECK(harmDb(asym, 1000, 2) > harmDb(asym, 1000, 3) - 3.0); CHECK(harmDb(asym, 1000, 2) > -30.0);
    double m = 0; for (size_t i = asym.size() / 2; i < asym.size(); ++i) m += asym[i]; NEAR(m / (asym.size() / 2.0), 0.0, 2e-3);
}
// the common oversampling setting (spec: 1x / 2x / 4x, default 2x)
namespace {
std::vector<float> runOs(int os, double g, double b, double db, double f, double sec = 2) {
    BiasShaper2x s; s.prepare(kFs); s.setOversample(os); const auto x = sine(db, sec, f); std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = static_cast<float>(s.process(0, x[i], g, b));
    return y;
}
}
TEST_CASE("BiasShaper: oversampling 1x / 2x / 4x - the default is 2x (BiasShaper4x: 4x), the alias of 15 kHz's 3rd harmonic falls with each step") {
    BiasShaper2x a; BiasShaper4x b; CHECK(a.oversample() == 2); CHECK(b.oversample() == 4);
    auto alias = [](int os) { const auto y = runOs(os, 8.0, 0.3, -10, 15000); return binDb(y, 3000, y.size() / 2, y.size()) - binDb(y, 15000, y.size() / 2, y.size()); };
    const double a1 = alias(1), a2 = alias(2), a4 = alias(4);
    INFO("alias re the fundamental: 1x " << a1 << " dB, 2x " << a2 << " dB, 4x " << a4 << " dB");
    CHECK(a1 > -45.0); CHECK(a2 < -60.0); CHECK(ost::notWorse(a4, a2));
}
TEST_CASE("BiasShaper: at every oversampling setting the small-signal gain is 1, the harmonics in the band are the same and the DC shift is removed") {
    for (int os : {1, 2, 4}) {
        NEAR(rmsDb(runOs(os, 4.0, 0.3, -60, 1000)) - -60.0, 0.0, 0.1);
        const auto y = runOs(os, 4.0, 0.3, -10, 1000), ref = runOs(2, 4.0, 0.3, -10, 1000);
        NEAR(harmDb(y, 1000, 2), harmDb(ref, 1000, 2), 0.5); NEAR(harmDb(y, 1000, 3), harmDb(ref, 1000, 3), 0.5);
        double m = 0; for (size_t i = y.size() / 2; i < y.size(); ++i) m += y[i]; NEAR(m / (y.size() / 2.0), 0.0, 2e-3);
    }
}
TEST_CASE("BiasShaper: changing the oversampling while running is clean") {
    BiasShaper2x s; s.prepare(kFs);
    const int seq[] = {2, 1, 4, 2, 1, 4, 1, 2}; int n = 0;
    for (int os : seq) { s.setOversample(os); for (int i = 0; i < 2400; ++i, ++n) { const double v = s.process(0, 0.5 * std::sin(2 * kPi * 1000.0 * n / kFs), 6.0, 0.3); REQUIRE(std::isfinite(v)); REQUIRE(std::abs(v) < 4.0); } }
}

// Unit A / B / C: where the saturation sets in, per channel (+-0.5 dB on the drive gain)
TEST_CASE("BiasShaper: the saturation onset of a channel moves by its dB and leaves the small-signal gain alone") {
    auto runCh = [](double db0, double db1, int ch) {
        BiasShaper2x s; s.prepare(kFs); s.setOnsetDb(0, db0); s.setOnsetDb(1, db1); const auto x = sine(-10, 2, 1000); std::vector<float> y(x.size());
        for (size_t i = 0; i < x.size(); ++i) y[i] = static_cast<float>(s.process(ch, x[i], 4.0, 0.3));
        return y;
    };
    const double h0 = harmDb(runCh(0, 0, 0), 1000, 3), hUp = harmDb(runCh(0.5, 0, 0), 1000, 3), hDown = harmDb(runCh(-0.5, 0, 0), 1000, 3);
    INFO("3rd harmonic: -0.5 dB " << hDown << ", 0 dB " << h0 << ", +0.5 dB " << hUp);
    CHECK(hUp > h0 + 0.3); CHECK(hDown < h0 - 0.3);
    CHECK(runCh(0, 0, 0) == run1(4.0, 0.3, -10));
    CHECK(runCh(0.5, -0.5, 1) == runCh(0, -0.5, 1));
    auto small = [&](double db) { BiasShaper2x s; s.prepare(kFs); s.setOnsetDb(0, db); const auto x = sine(-60, 2, 1000); std::vector<float> y(x.size()); for (size_t i = 0; i < x.size(); ++i) y[i] = static_cast<float>(s.process(0, x[i], 4.0, 0.3)); return rmsDb(y) - -60.0; };
    NEAR(small(0.5), 0.0, 0.1); NEAR(small(-0.5), 0.0, 0.1);
}
