#include "doctest.h"
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
