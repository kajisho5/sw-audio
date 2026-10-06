#include "doctest.h"
#include "dy12/dy12.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::dy12;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
double level(Set s, const std::vector<float>& x) { auto p = make(s); return rmsDb(run(p, x)); }
}

TEST_CASE("DY12 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"dy12.squash", "dy12.blend", "dy12.upward", "dy12.tone", "dy12.speed", "dy12.out"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Squash].min == 0); CHECK(s[Squash].max == 10); CHECK(s[Squash].def == 5);
    CHECK(s[Blend].min == 0); CHECK(s[Blend].max == 100); CHECK(s[Blend].def == 30);
    CHECK(s[Upward].min == 0); CHECK(s[Upward].max == 10); CHECK(s[Upward].def == 0);
    CHECK(s[Tone].min == -6); CHECK(s[Tone].max == 6); CHECK(s[Tone].def == 0);
    CHECK(s[Speed].labels == std::vector<std::string>{"Fast", "Med", "Slow", "Auto"}); CHECK(s[Speed].def == 3);
    CHECK(s[Output].min == -10); CHECK(s[Output].max == 10); CHECK(s[Output].def == 0);
    for (const auto& p : s) CHECK(p.automatable);
}
TEST_CASE("DY12 Blend 0 = dry, Blend 100 = the squashed signal alone") {
    const auto x = sine(-12, 3);
    NEAR(level({{Blend, 0}}, x), -12.0, 0.1);
    const double wet = level({{Blend, 100}, {Squash, 10}}, x);    // threshold -40 dBFS, 10:1: 28 dB over -> about -25 dB, then made up
    CHECK(std::abs(wet - -12.0) < 3.0);                           // the loss is made up automatically
}
TEST_CASE("DY12 the squashed side is a 10:1 compressor with automatic makeup (threshold = -4 x Squash dBFS)") {
    // Blend 100: output level changes by only 1/10 of the input change above the threshold
    auto out = [](double in) { return level({{Blend, 100}, {Squash, 5}, {Speed, 1}}, sine(in, 3)); };   // threshold -20 dBFS, makeup 7.2 dB at the -12 dBFS reference
    NEAR(out(-2) - out(-8), 0.6, 0.4);                                                              // 6 dB in -> 0.6 dB out
}
TEST_CASE("DY12 parallel blend raises the tails and keeps the hit (the head passes the attack time uncompressed)") {
    const auto drums = [] { std::vector<float> x(48000 * 2, 0.0f); for (size_t i = 0; i < x.size(); i += 12000) for (size_t k = 0; k < 11000; ++k) x[i + k] = static_cast<float>(0.8 * std::exp(-static_cast<double>(k) / 1600.0) * std::sin(2 * kPi * 120 * k / kFs)); return x; }();
    auto stats = [&](Set s) { auto p = make(s); const auto y = run(p, drums); return std::make_pair(peakDb(y, 24000, y.size()), rmsDb(y, 24000 + 8000, 24000 + 10000)); };
    const auto dry = stats({{Blend, 0}}), mix = stats({{Blend, 40}, {Squash, 8}, {Speed, 0}});   // Fast: 50 ms release, so the tail comes up
    CHECK(mix.second > dry.second + 3.0);          // the tail (100..125 ms after the hit, GR released, makeup still on) comes up
    CHECK(mix.first < dry.first + 6.0);            // the head is not lifted by more than the slow attack lets through
}
TEST_CASE("DY12 Upward lifts what is quiet (up to +12 dB), not what is loud, and not below -60 dBFS") {
    auto gain = [](double db, double up) { return level({{Blend, 0}, {Upward, up}}, sine(db, 3)) - db; };
    NEAR(gain(-50, 10), 12.0, 2.0);     // well below -40: full +12 dB
    CHECK(gain(-30, 10) > 5.0); CHECK(gain(-30, 10) < 11.0);
    NEAR(gain(-6, 10), 0.0, 1.0);       // loud: no lift
    NEAR(gain(-50, 5), 6.0, 2.0);       // Upward 5 = half
    NEAR(gain(-70, 10), 0.0, 1.5);      // below -60: not lifted (no noise boost)
    NEAR(gain(-50, 0), 0.0, 0.1);
}
TEST_CASE("DY12 Tone tilts only the squashed side") {
    auto hi = [](double tone, double f) { return level({{Blend, 100}, {Squash, 0}, {Tone, tone}}, sine(-20, 2, f)); };   // Squash 0: threshold 0 dBFS, signal passes uncompressed
    CHECK(hi(6, 8000) - hi(6, 200) > 8.0);        // Bright: highs up, lows down
    CHECK(hi(-6, 200) - hi(-6, 8000) > 8.0);      // Dark
    NEAR(level({{Blend, 0}, {Tone, 6}}, sine(-20, 2, 8000)), -20.0, 0.1);   // the dry side is untouched
}
TEST_CASE("DY12 Speed: 1 ms / 50 ms, 5 / 150, 20 / 400") {
    auto rel = [](int speed) {
        auto p = make({{Blend, 100}, {Squash, 8}, {Speed, static_cast<double>(speed)}}); run(p, sine(-6, 4));
        const double g0 = p.gainReductionDb(); const auto q = sine(-70, 5.0);
        for (size_t off = 0; off + 48 <= q.size(); off += 48) { std::vector<float> a(q.begin() + off, q.begin() + off + 48), b = a; float* c[2] = {a.data(), b.data()}; p.process(c, 2, 48); if (p.gainReductionDb() > g0 * 0.368) return 1000.0 * (off + 48) / kFs; }
        return 99999.0;
    };
    NEAR(rel(0), 50.0, 25.0); NEAR(rel(1), 150.0, 60.0); NEAR(rel(2), 400.0, 150.0);
    const double a = rel(3); CHECK(a > 30.0); CHECK(a < 3000.0);   // Auto: signal dependent
}
TEST_CASE("DY12 silence stays silent, extreme input finite, latency 0") {
    auto p = make({{Blend, 100}, {Squash, 10}, {Upward, 10}, {Tone, 6}});
    for (float v : run(p, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f);
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : run(p, x)) REQUIRE(std::isfinite(v));
    CHECK(p.latencySamples() == 0);
}
