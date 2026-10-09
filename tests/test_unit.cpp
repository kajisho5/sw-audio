// Unit A / B / C: fixed component tolerances (spec common function)
#include "doctest.h"
#include "sw/unit.hpp"
#include <algorithm>
#include <cmath>
#include <initializer_list>
using namespace sw;

TEST_CASE("Unit A is the reference: no deviation at all") {
    for (int ch = 0; ch < 2; ++ch) for (int slot = 0; slot < 16; ++slot) {
        CHECK(Unit::gainDb(0, ch, slot) == 0.0); CHECK(Unit::gainLin(0, ch, slot) == 1.0); CHECK(Unit::freqMul(0, ch, slot) == 1.0); CHECK(Unit::satDb(0, ch, slot) == 0.0);
    }
    CHECK(Unit::gainDb(-3, 0, 0) == 0.0);   // anything below A is A
}
TEST_CASE("Unit B and C stay inside the spec's limits and use them") {
    for (int unit : {1, 2}) {
        double gmax = 0, fmax = 0, smax = 0, gsum = 0, fsum = 0, ssum = 0; int n = 0;
        for (int ch = 0; ch < 2; ++ch) for (int slot = 0; slot < 2000; ++slot) {
            const double g = Unit::gainDb(unit, ch, slot), f = Unit::freqMul(unit, ch, slot) - 1.0, s = Unit::satDb(unit, ch, slot);
            CHECK(std::abs(g) <= 0.3 + 1e-12); CHECK(std::abs(f) <= 0.03 + 1e-12); CHECK(std::abs(s) <= 0.5 + 1e-12);
            gmax = std::max(gmax, std::abs(g)); fmax = std::max(fmax, std::abs(f)); smax = std::max(smax, std::abs(s)); gsum += g; fsum += f; ssum += s; ++n;
        }
        CHECK(gmax > 0.29); CHECK(fmax > 0.029); CHECK(smax > 0.49);               // the whole range is used
        CHECK(std::abs(gsum / n) < 0.01); CHECK(std::abs(fsum / n) < 0.001); CHECK(std::abs(ssum / n) < 0.02);   // and it is centred
    }
}
TEST_CASE("Unit B and C are two different units, and the left and the right channel differ") {
    int sameBC = 0, sameLR = 0;
    for (int slot = 0; slot < 64; ++slot) {
        if (Unit::gainDb(1, 0, slot) == Unit::gainDb(2, 0, slot)) ++sameBC;
        if (Unit::gainDb(1, 0, slot) == Unit::gainDb(1, 1, slot)) ++sameLR;
        CHECK(Unit::freqMul(1, 0, slot) != Unit::freqMul(2, 0, slot));
        CHECK(Unit::freqMul(1, 0, slot) != Unit::freqMul(1, 1, slot));
        CHECK(Unit::satDb(2, 0, slot) != Unit::satDb(2, 1, slot));
    }
    CHECK(sameBC == 0); CHECK(sameLR == 0);
    // the three kinds of one control are different numbers too
    CHECK(Unit::value(1, 0, 3, Unit::Gain) != Unit::value(1, 0, 3, Unit::Freq)); CHECK(Unit::value(1, 0, 3, Unit::Freq) != Unit::value(1, 0, 3, Unit::Sat));
}
TEST_CASE("Unit B and C are fixed: the same numbers in every session (golden values)") {
    // if these change, every saved session that uses Unit B or C sounds different: change them only on purpose
    const double g10 = Unit::value(1, 0, 0, Unit::Gain), g11 = Unit::value(1, 1, 0, Unit::Gain), g20 = Unit::value(2, 0, 0, Unit::Gain), f13 = Unit::value(1, 1, 5, Unit::Freq);
    INFO("B/L gain " << g10 << ", B/R gain " << g11 << ", C/L gain " << g20 << ", B/R slot 5 freq " << f13);
    CHECK(g10 == doctest::Approx(0.26971145759800796).epsilon(1e-12)); CHECK(g11 == doctest::Approx(-0.25537988910158838).epsilon(1e-12)); CHECK(g20 == doctest::Approx(0.56874995840497555).epsilon(1e-12)); CHECK(f13 == doctest::Approx(-0.7443808835958301).epsilon(1e-12));
}
TEST_CASE("Unit: a unit above C is C") {
    CHECK(Unit::gainDb(7, 0, 1) == Unit::gainDb(2, 0, 1));
}
