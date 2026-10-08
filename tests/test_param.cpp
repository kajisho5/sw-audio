#include "doctest.h"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include <cmath>
using namespace sw;

TEST_CASE("LIN curve maps normalized to value and back") {
    ParamSpec p{"g", "Gain", -15.0, 15.0, 0.0, Curve::Lin};
    CHECK(p.toValue(0.5) == doctest::Approx(0.0));
    CHECK(p.toValue(1.0) == doctest::Approx(15.0));
    CHECK(p.toNorm(7.5) == doctest::Approx(0.75));
}
TEST_CASE("LOG curve is geometric between min and max") {
    ParamSpec p{"f", "Freq", 1500.0, 16000.0, 8000.0, Curve::Log};
    CHECK(p.toValue(0.0) == doctest::Approx(1500.0));
    CHECK(p.toValue(1.0) == doctest::Approx(16000.0));
    CHECK(p.toValue(0.5) == doctest::Approx(std::sqrt(1500.0 * 16000.0)));
    CHECK(p.toNorm(p.toValue(0.3)) == doctest::Approx(0.3));
}
TEST_CASE("SKW curve uses x^k") {
    ParamSpec p{"a", "Attack", 0.1, 200.0, 10.0, Curve::Skew, 3.0};
    CHECK(p.toValue(0.5) == doctest::Approx(0.1 + 199.9 * 0.125));
    CHECK(p.toNorm(p.toValue(0.42)) == doctest::Approx(0.42));
}
TEST_CASE("STEP curve snaps to the nearest listed value") {
    ParamSpec p{"h", "HPF", 0.0, 200.0, 0.0, Curve::Step, 1.0, {0, 40, 80, 120, 200}};
    CHECK(p.numSteps() == 5);
    CHECK(p.toValue(0.5) == doctest::Approx(80.0));
    CHECK(p.toValue(0.6) == doctest::Approx(80.0));
    CHECK(p.toNorm(120.0) == doctest::Approx(0.75));
}
TEST_CASE("normalized input outside 0..1 is clamped") {
    ParamSpec p{"g", "Gain", -15.0, 15.0, 0.0, Curve::Lin};
    CHECK(p.toValue(-0.2) == doctest::Approx(-15.0));
    CHECK(p.toValue(1.5) == doctest::Approx(15.0));
}
TEST_CASE("a value that is not a number reads as the default; a value below a log range reads as its minimum (a damaged session or preset)") {
    const double nan = std::nan("");
    const ParamSpec lin{"g", "Gain", -15.0, 15.0, 3.0, Curve::Lin};
    CHECK(lin.toValue(nan) == doctest::Approx(3.0));
    CHECK(lin.toNorm(nan) == doctest::Approx(lin.toNorm(3.0)));
    const ParamSpec lg{"f", "Freq", 20.0, 20000.0, 1000.0, Curve::Log};
    CHECK(lg.toValue(nan) == doctest::Approx(1000.0));
    CHECK(lg.toNorm(-5.0) == 0.0);              // log of a negative number: no NaN
    CHECK(lg.toNorm(0.0) == 0.0);
    CHECK(lg.toValue(lg.toNorm(-5.0)) == doctest::Approx(20.0));
    const ParamSpec rev{"w", "Width", 0.4, 2.0, 1.0, Curve::Lin, 1.0, {}, "", {}, nullptr, nullptr, 1.0, true, true};
    CHECK(rev.toValue(nan) == doctest::Approx(1.0));
    const ParamSpec st{"h", "HPF", 0.0, 200.0, 40.0, Curve::Step, 1.0, {0, 40, 80}};
    CHECK(st.toValue(nan) == 40.0);
    CHECK(st.toNorm(nan) == doctest::Approx(0.5));
    const ParamSpec sk{"a", "Attack", 0.1, 200.0, 10.0, Curve::Skew, 3.0};
    CHECK(std::isfinite(sk.toValue(sk.toNorm(nan))));
    CHECK(std::isfinite(lin.toValue(INFINITY)));
    CHECK(std::isfinite(lg.toNorm(INFINITY)));
}
TEST_CASE("linear smoother reaches the target exactly after the ramp time") {
    LinearSmoother s;
    s.reset(48000.0, 20.0, 0.0);
    s.setTarget(1.0);
    double v = 0, prev = -1;
    bool mono = true;
    for (int i = 0; i < 480; ++i) { v = s.next(); if (v < prev) mono = false; prev = v; }
    CHECK(v == doctest::Approx(0.5).epsilon(0.01));
    for (int i = 480; i < 960; ++i) { v = s.next(); if (v < prev) mono = false; prev = v; }
    CHECK(v == 1.0);
    CHECK(mono);
    CHECK_FALSE(s.isSmoothing());
}

TEST_CASE("skip(n) advances the smoother exactly like n calls to next()") {
    LinearSmoother a, b;
    a.reset(48000.0, 20.0, -15.0); b.reset(48000.0, 20.0, -15.0);
    a.setTarget(15.0); b.setTarget(15.0);
    for (int i = 0; i < 300; ++i) a.next();
    CHECK(b.skip(300) == doctest::Approx(a.current()));
    CHECK(b.skip(5000) == 15.0);
}

TEST_CASE("SymLog curve is symmetric about the middle and logarithmic away from it") {
    ParamSpec p{"t", "t", -2000, 2000, 35, Curve::SymLog};
    CHECK(p.toValue(0.5) == doctest::Approx(0.0).epsilon(1e-9)); CHECK(p.toValue(0.0) == doctest::Approx(-2000.0)); CHECK(p.toValue(1.0) == doctest::Approx(2000.0));
    for (double x : {0.1, 0.25, 0.4}) CHECK(p.toValue(x) == doctest::Approx(-p.toValue(1.0 - x)));
    double prev = -1e9; for (int i = 0; i <= 100; ++i) { const double v = p.toValue(i / 100.0); CHECK(v >= prev); prev = v; }
    for (double v : {-2000.0, -500.0, -35.0, -1.0, 1.0, 35.0, 500.0, 2000.0}) CHECK(p.toValue(p.toNorm(v)) == doctest::Approx(v).epsilon(1e-6));
    CHECK(p.toValue(p.toNorm(0.0)) == doctest::Approx(0.0).scale(1.0).epsilon(1e-9));
    CHECK(p.toNorm(35.0) > 0.7); CHECK(p.toNorm(35.0) < 0.78);   // +35 Hz sits at about three quarters (log scale away from the middle)
}
