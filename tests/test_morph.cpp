#include "doctest.h"
#include "sw/morph.hpp"
using namespace sw;

TEST_CASE("morph moves frequencies geometrically (normalized LOG interpolation)") {
    ParamSpec f{"f", "Freq", 20, 20000, 1000, Curve::Log, 1, {}, "Hz"};
    CHECK(morphValue(f, 100.0, 10000.0, 0.5) == doctest::Approx(1000.0));
}
TEST_CASE("morph moves dB gains linearly") {
    ParamSpec g{"g", "Gain", -15, 15, 0, Curve::Lin, 1, {}, "dB"};
    CHECK(morphValue(g, -12.0, 12.0, 0.25) == doctest::Approx(-6.0));
}
TEST_CASE("stepped parameters switch at morph 0.5") {
    ParamSpec s{"h", "HPF", 0, 200, 0, Curve::Step, 1, {0, 40, 80, 120, 200}, "Hz"};
    CHECK(morphValue(s, 40.0, 200.0, 0.49) == 40.0);
    CHECK(morphValue(s, 40.0, 200.0, 0.5) == 200.0);
}
TEST_CASE("morph ends exactly on A and B and clamps outside 0..1") {
    ParamSpec g{"g", "Gain", -15, 15, 0, Curve::Lin, 1, {}, "dB"};
    CHECK(morphValue(g, -3.3, 7.7, 0.0) == doctest::Approx(-3.3));
    CHECK(morphValue(g, -3.3, 7.7, 1.0) == doctest::Approx(7.7));
    CHECK(morphValue(g, -3.3, 7.7, 1.7) == doctest::Approx(7.7));
    CHECK(morphValue(g, -3.3, 7.7, -1.0) == doctest::Approx(-3.3));
}
