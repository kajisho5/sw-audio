#include "doctest.h"
#include "sw/gate.hpp"
#include <cmath>
#include <vector>
using namespace sw;
namespace { const double kFs = 48000.0; double db(double g) { return 20 * std::log10(g); } }

TEST_CASE("gate: closes to the range after hold + release, opens within the attack") {
    GateEngine g; g.prepare(kFs); g.set(GateEngine::Mode::Gate, -40, -40, 0.1, 50, 100);
    double gain = 1;
    for (int i = 0; i < 48000; ++i) gain = g.process(0.001);  // -60 dBFS: below threshold
    CHECK(db(gain) == doctest::Approx(-40).epsilon(0.01));
    for (int i = 0; i < 96; ++i) gain = g.process(0.5);       // 2 ms of a loud hit
    CHECK(db(gain) > -0.5);
}
TEST_CASE("gate: hold keeps it open after the key drops") {
    GateEngine g; g.prepare(kFs); g.set(GateEngine::Mode::Gate, -40, -40, 0.1, 50, 100);
    for (int i = 0; i < 4800; ++i) g.process(0.5);
    double gain = 0;
    for (int i = 0; i < 1920; ++i) gain = g.process(0.0);  // 40 ms < 50 ms hold (+10 ms detector decay)
    CHECK(db(gain) > -0.1);
}
TEST_CASE("gate: 4 dB hysteresis stops chatter around the threshold") {
    GateEngine g; g.prepare(kFs); g.set(GateEngine::Mode::Gate, -40, -40, 0.1, 0, 5);
    int toggles = 0; bool was = false;
    for (int i = 0; i < 48000; ++i) {
        const double lvl = std::pow(10.0, (-40.0 + 1.5 * std::sin(i * 0.01)) / 20.0);  // +-1.5 dB around threshold
        g.process(lvl);
        if (g.isOpen() != was) { ++toggles; was = g.isOpen(); }
    }
    CHECK(toggles <= 1);
}
TEST_CASE("expander: 10 dB under the threshold is pulled down a further 10 dB (1:2)") {
    GateEngine g; g.prepare(kFs); g.set(GateEngine::Mode::Expand, -30, -60, 0.1, 0, 20);
    double gain = 1;
    for (int i = 0; i < 48000; ++i) gain = g.process(std::pow(10.0, -40.0 / 20.0));
    CHECK(db(gain) == doctest::Approx(-10).epsilon(0.02));
}
TEST_CASE("ducker: a loud key pulls the gain down to the range, a quiet key lets it recover") {
    GateEngine g; g.prepare(kFs); g.set(GateEngine::Mode::Duck, -30, -12, 1, 0, 100);
    double gain = 1;
    for (int i = 0; i < 24000; ++i) gain = g.process(0.5);
    CHECK(db(gain) == doctest::Approx(-12).epsilon(0.01));
    for (int i = 0; i < 48000; ++i) gain = g.process(0.0);
    CHECK(db(gain) > -0.1);
}
