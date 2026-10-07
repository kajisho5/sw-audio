#include "doctest.h"
#include "lv01/lv01.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv01;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// speech-like: 1 kHz + 2.2 kHz bursts at `lvl` dBFS rms (0.4 s) in a room noise `room` dBFS (0.6 s gaps)
std::vector<float> talk(double seconds, double lvl, double room) {
    const size_t n = static_cast<size_t>(seconds * kFs); auto a = sine(lvl, seconds, 1000), b = sine(lvl - 6, seconds, 2200), nz = noise(room, seconds, 7); std::vector<float> y(n);
    for (size_t i = 0; i < n; ++i) y[i] = nz[i] + (((i / 48000) % 1 == 0 && (i % 48000) < 19200) ? a[i] + b[i] : 0.0f);
    return y;
}
}

TEST_CASE("LV01 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Use].labels == std::vector<std::string>{"Narration", "Stream", "Meeting", "Singing"}); CHECK(s[Use].def == 1);
    CHECK(s[Voice].min == 0); CHECK(s[Voice].max == 100); CHECK(s[Voice].def == 62);
    CHECK(s[Mute].automatable == false); CHECK(s[Use].automatable == true);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV01 Voice 0 is a bit-exact bypass") {
    auto p = make({{Voice, 0}}); const auto x = talk(3.0, -20, -50); const auto y = run(p, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]);
}
TEST_CASE("LV01 stage values follow Use and Voice") {
    auto p = make({{Use, Stream}, {Voice, 100}}); auto s = p.stage();
    CHECK(s.noiseDepthDb == 14); CHECK(s.lowCutHz == doctest::Approx(80)); CHECK(s.compRatio == doctest::Approx(4)); CHECK(s.ceilingDb == -1);
    p.setParam(Voice, 50); s = p.stage(); CHECK(s.noiseDepthDb == 7); CHECK(s.presenceDb == doctest::Approx(1.5)); CHECK(s.compRatio == doctest::Approx(2.5));
    p.setParam(Use, Singing); p.setParam(Voice, 100); s = p.stage(); CHECK(s.airDb == 3.0); CHECK(s.noiseDepthDb == 6);
    CHECK(p.stage().lowCutHz == doctest::Approx(40));
}
TEST_CASE("LV01 noise: the room is pulled down, the voice is not") {
    const auto x = talk(8.0, -24, -52);
    auto p = make({{Use, Narration}, {Voice, 100}}); const auto y = run(p, x);
    // a gap late in the run (0.45..0.95 s into the last second), the noise only
    const size_t a = 7 * 48000 + 24000, b = 7 * 48000 + 46000;
    CHECK(rmsDb(y, a, b) < rmsDb(x, a, b) - 8.0);
    // the 1 kHz burst keeps its level within 4 dB (the comp and EQ move it a little, the expander does not)
    const double inB = binDb(x, 1000, 7 * 48000 + 4000, 7 * 48000 + 15000), outB = binDb(y, 1000, 7 * 48000 + 4000, 7 * 48000 + 15000);
    CHECK(std::abs(outB - inB) < 4.0);
    // Voice halves the depth
    auto q = make({{Use, Narration}, {Voice, 50}}); const auto z = run(q, x); CHECK(rmsDb(z, a, b) > rmsDb(y, a, b) + 2.0);
}
TEST_CASE("LV01 noise floor follows the room") {
    auto p = make({{Voice, 100}}); run(p, noise(-60, 4.0, 3)); const double lo = p.noiseFloorDb(1);
    auto q = make({{Voice, 100}}); run(q, noise(-55, 4.0, 3)); const double hi = q.noiseFloorDb(1);
    CHECK(hi > lo + 3.0); CHECK(hi < -50.0);
}
TEST_CASE("LV01 EQ: low cut, presence boost, no change in the middle") {
    auto p = make({{Use, Meeting}, {Voice, 100}}); const auto lo = run(p, sine(-30, 2.0, 40)), mid = run(p, sine(-30, 2.0, 1000)), pr = run(p, sine(-30, 2.0, 3500));
    CHECK(rmsDb(lo) < -30 - 3.0 - 3.0);   // 40 Hz is under the 100 Hz cut (and the sine rms is -30 dBFS to begin with, with the expander off at this level)
    CHECK(rmsDb(pr) > rmsDb(mid) + 2.0);
}
TEST_CASE("LV01 comp evens out levels; limiter holds -1 dBFS; output is finite") {
    auto p = make({{Use, Stream}, {Voice, 100}}); auto loud = sine(-10, 2.0, 700), quiet = sine(-30, 2.0, 700);
    const auto yl = run(p, loud), yq = run(p, quiet);
    CHECK((rmsDb(yl) - rmsDb(yq)) < 20.0 - 4.0);
    auto q = make({{Voice, 100}}); std::vector<float> x = noise(6, 3.0, 5); for (auto& v : x) v *= 4.0f;
    for (float v : run(q, x)) { REQUIRE(std::isfinite(v)); REQUIRE(std::abs(v) <= 0.89126f + 1e-6f); }
}
TEST_CASE("LV01 Mute silences in 5 ms and returns") {
    auto p = make({{Voice, 0}}); const auto x = sine(-20, 1.0, 500);
    p.setParam(Mute, 1); const auto y = run(p, x); CHECK(std::abs(y[100]) > 0.0f); for (size_t i = 400; i < 5000; ++i) REQUIRE(y[i] == 0.0f);
    p.setParam(Mute, 0); const auto z = run(p, x); for (size_t i = 400; i < 5000; ++i) REQUIRE(z[i] == x[i]);
    auto q = make({{Voice, 100}, {Mute, 1}}); const auto w = run(q, x); for (size_t i = 400; i < 5000; ++i) REQUIRE(w[i] == 0.0f);
}
TEST_CASE("LV01 mono, odd blocks, before prepare") {
    auto p = make(); std::vector<float> l = talk(2.0, -20, -50); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; p.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    Processor q; std::vector<float> z(256, 0.3f); float* c[1] = {z.data()}; q.process(c, 1, 256); CHECK(z[0] == 0.3f); q.snapToTargets();
}
