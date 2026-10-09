// sw::LevelLearner (CS03 "set the input level"): the gain that brings the average to -18 dBFS RMS without the peak going over -6 dBFS
#include "doctest.h"
#include "sw/level_learner.hpp"
#include "tu.hpp"
using namespace tu;
namespace {
void feed(sw::LevelLearner& l, const std::vector<float>& x) { for (float v : x) l.add(v, v); }
std::vector<float> tone(double rmsDb, double seconds, double hz = 220.0) { const size_t n = static_cast<size_t>(seconds * kFs); std::vector<float> x(n); const double a = std::pow(10.0, (rmsDb + 3.0103) / 20.0); for (size_t i = 0; i < n; ++i) x[i] = static_cast<float>(a * std::sin(2.0 * kPi * hz * static_cast<double>(i) / kFs)); return x; }
}  // namespace

TEST_CASE("LevelLearner: a steady source is brought to -18 dBFS RMS (its peak is far from the ceiling)") {
    sw::LevelLearner l; l.prepare(kFs); l.start(5.0);
    feed(l, tone(-30.0, 5.0));
    CHECK(!l.learning());
    const auto r = l.finish();
    REQUIRE(r.ok);
    NEAR(r.rmsDb, -30.0, 0.1); NEAR(r.peakDb, -26.99, 0.1); NEAR(r.gainDb, 12.0, 0.1);
}

TEST_CASE("LevelLearner: a peaky source is held by the peak ceiling (-6 dBFS) and ends below the target RMS; a hot one is turned down") {
    {   // a noise floor (-40 dBFS RMS) with 1 ms clicks of a 440 Hz tone at -8 dBFS every 0.1 s: the average is about -31 dBFS, the peak -8
        auto x = noise(-40.0, 5.0, 3); const double a = std::pow(10.0, -8.0 / 20.0);
        for (size_t i = 0; i < x.size(); ++i) if (std::fmod(static_cast<double>(i) / kFs, 0.1) < 0.001) x[i] += static_cast<float>(a * std::sin(2.0 * kPi * 440.0 * static_cast<double>(i) / kFs));
        sw::LevelLearner l; l.prepare(kFs); l.start(5.0); feed(l, x); const auto r = l.finish(); REQUIRE(r.ok);
        INFO("RMS " << r.rmsDb << ", peak " << r.peakDb << ", gain " << r.gainDb);
        NEAR(r.gainDb, -6.0 - r.peakDb, 0.01); CHECK(r.gainDb < sw::LevelLearner::kTargetRmsDb - r.rmsDb - 3.0); CHECK(r.gainDb < 3.0);
    }
    { sw::LevelLearner l; l.prepare(kFs); l.start(5.0); feed(l, tone(-10.0, 5.0)); const auto r = l.finish(); REQUIRE(r.ok); NEAR(r.gainDb, -8.0, 0.1); }   // -10 dBFS RMS: 8 dB down
}

TEST_CASE("LevelLearner: pauses are left out; silence, a blip, an unstarted or an early-stopped listening are no result") {
    {   // 1 s of tone, 4 s of nothing: the average is the tone's
        auto x = tone(-30.0, 1.0); x.resize(static_cast<size_t>(5 * kFs), 0.0f);
        sw::LevelLearner l; l.prepare(kFs); l.start(5.0); feed(l, x); const auto r = l.finish(); REQUIRE(r.ok); NEAR(r.rmsDb, -30.0, 0.1);
    }
    { sw::LevelLearner l; l.prepare(kFs); l.start(5.0); feed(l, std::vector<float>(static_cast<size_t>(5 * kFs), 0.0f)); CHECK(!l.finish().ok); }
    { sw::LevelLearner l; l.prepare(kFs); l.start(5.0); feed(l, tone(-70.0, 5.0)); CHECK(!l.finish().ok); }   // below the pause level: nothing is playing
    { sw::LevelLearner l; l.prepare(kFs); l.start(5.0); auto x = tone(-20.0, 0.3); x.resize(static_cast<size_t>(5 * kFs), 0.0f); feed(l, x); CHECK(!l.finish().ok); }   // 0.3 s is too little
    { sw::LevelLearner l; l.prepare(kFs); CHECK(!l.finish().ok); CHECK(!l.learning()); }
    { sw::LevelLearner l; l.prepare(kFs); l.start(5.0); feed(l, tone(-24.0, 2.0)); CHECK(l.learning()); CHECK(l.progress() == doctest::Approx(0.4).epsilon(0.01)); const auto r = l.finish(); CHECK(r.ok); CHECK(!l.learning()); NEAR(r.gainDb, 6.0, 0.1); }   // stopped by hand after 2 s
}
