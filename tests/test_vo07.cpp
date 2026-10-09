#include "doctest.h"
#include "vo07/vo07.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::vo07;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
double lvl(Set set, double hz, double db = -18.0) { auto p = make(set); const auto y = run(p, sine(db, 3.0, hz)); return rmsDb(y, 96000, 144000) - db; }
}

TEST_CASE("VO07 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"vo07.hpf", "vo07.deess", "vo07.breath", "vo07.body", "vo07.presence", "vo07.air", "vo07.comp", "vo07.level", "vo07.plate", "vo07.echo", "vo07.out", "vo07.unit"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Hpf].min == 20); CHECK(s[Hpf].max == 300); CHECK(s[Hpf].def == 80); CHECK(s[Hpf].curve == Curve::Log);
    for (int i : {Deess, Breath, Comp, Plate, Echo}) { CHECK(s[static_cast<size_t>(i)].min == 0); CHECK(s[static_cast<size_t>(i)].max == 10); CHECK(s[static_cast<size_t>(i)].def == 0); }
    for (int i : {Body, Presence, Air}) { CHECK(s[static_cast<size_t>(i)].min == -6); CHECK(s[static_cast<size_t>(i)].max == 6); CHECK(s[static_cast<size_t>(i)].def == 0); }
    CHECK(s[Level].min == -12); CHECK(s[Level].max == 12); CHECK(s[Level].def == 0);
    CHECK(s[Output].min == -10); CHECK(s[Output].max == 10); CHECK(s[Output].def == 0);
}
TEST_CASE("VO07 no delay is reported; silence is silence; flat at the defaults") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
    NEAR(lvl({}, 1000), 0.0, 0.3); NEAR(lvl({}, 4000), 0.0, 0.3); NEAR(lvl({}, 200), 0.0, 0.5);
}
TEST_CASE("VO07 HPF cleans the low end") {
    CHECK(lvl({{Hpf, 300}}, 60) < -20.0); NEAR(lvl({{Hpf, 300}}, 3000), 0.0, 0.3);
    NEAR(lvl({{Hpf, 20}}, 100), 0.0, 0.5);
    NEAR(lvl({{Hpf, 80}}, 40), -12.0, 3.0);    // 12 dB/oct, an octave below
}
TEST_CASE("VO07 Body / Presence / Air: +-6 dB at 200 Hz / 3 kHz / 12 kHz") {
    NEAR(lvl({{Body, 6}}, 200), 6.0, 0.8); NEAR(lvl({{Body, -6}}, 200), -6.0, 0.8);
    NEAR(lvl({{Presence, 6}}, 3000), 6.0, 0.8); NEAR(lvl({{Presence, -6}}, 3000), -6.0, 0.8);
    NEAR(lvl({{Air, 6}}, 12000), 6.0, 1.2); NEAR(lvl({{Air, -6}}, 12000), -6.0, 1.2);
    NEAR(lvl({{Presence, 6}}, 200), 0.0, 1.0);   // a bell: far from its centre nothing changes
}
TEST_CASE("VO07 Level is a gain after the compressor") {
    NEAR(lvl({{Level, 6}}, 1000), 6.0, 0.2); NEAR(lvl({{Level, -12}}, 1000), -12.0, 0.2);
    // with the compressor, the louder part comes down and the quiet part keeps its level (the compressor sits before Level)
    auto step = [&](Set set) { auto p = make(set); std::vector<float> x = sine(-8, 3.0, 1000); const auto q = sine(-30, 3.0, 1000); x.insert(x.end(), q.begin(), q.end()); const auto y = run(p, x);
                               return std::pair<double, double>{rmsDb(y, 96000, 144000), rmsDb(y, 240000, 288000)}; };
    const auto off = step({}), on = step({{Comp, 8}});
    CHECK((on.first - on.second) < (off.first - off.second) - 6.0);   // the difference between loud and quiet shrinks by more than 6 dB
}
TEST_CASE("VO07 De-ess takes sibilance only") {
    // a hiss: white noise high-passed (second difference), at -18 dBFS; and a low vowel-like tone
    auto hiss = [&]() { auto x = noise(-30, 3.0, 11); std::vector<float> y(x.size(), 0.0f); for (size_t i = 2; i < x.size(); ++i) y[i] = 0.5f * (x[i] - 2.0f * x[i - 1] + x[i - 2]); return y; };
    auto lowered = [&](double deess, const std::vector<float>& x) { auto p = make({{Deess, deess}}); const auto y = run(p, x); return rmsDb(y, 96000, 144000) - rmsDb(x, 96000, 144000); };
    const auto h = hiss();
    CHECK(lowered(10, h) < -3.0); CHECK(lowered(5, h) < -1.0); CHECK(lowered(5, h) > lowered(10, h) + 1.0);
    NEAR(lowered(0, h), 0.0, 0.5);
    NEAR(lowered(10, sine(-24, 3.0, 500)), 0.0, 0.5);
}
TEST_CASE("VO07 Breath: a quiet noise-like stretch after a phrase goes down; a quiet tone does not") {
    auto tail = [&](double breath, bool noiseTail) {
        auto p = make({{Breath, breath}});
        std::vector<float> x = sine(-16, 4.0, 220); std::vector<float> t = noiseTail ? noise(-50, 1.0, 7) : sine(-50, 1.0, 220); x.insert(x.end(), t.begin(), t.end());
        const auto y = run(p, x); return rmsDb(y, 4 * 48000 + 24000, 5 * 48000 - 2400) - rmsDb(x, 4 * 48000 + 24000, 5 * 48000 - 2400);
    };
    CHECK(tail(10, true) < -10.0); NEAR(tail(5, true), -7.5, 3.0); NEAR(tail(0, true), 0.0, 0.3);
    NEAR(tail(10, false), 0.0, 0.5);   // low zero-crossing rate: a soft note is not a breath
    // the phrase itself is untouched
    auto p = make({{Breath, 10}}); const auto x = sine(-16, 3.0, 220); NEAR(rmsDb(run(p, x), 48000, 144000), rmsDb(x, 48000, 144000), 0.2);
}
TEST_CASE("VO07 Plate and Echo are sends after Level") {
    auto tailDb = [&](Set set, double from, double to) { auto p = make(set); std::vector<float> x(static_cast<size_t>(3 * kFs), 0.0f); for (int i = 0; i < 4800; ++i) x[static_cast<size_t>(i)] = 0.3f * static_cast<float>(std::sin(2 * kPi * 440.0 * i / kFs));
                                                         const auto y = run(p, x); return rmsDb(y, static_cast<size_t>(from * kFs), static_cast<size_t>(to * kFs)); };
    CHECK(tailDb({}, 0.5, 2.0) < -140.0);                                  // nothing comes back with the sends at 0
    CHECK(tailDb({{Plate, 10}}, 0.5, 2.0) > -60.0);                        // a reverb tail
    CHECK(tailDb({{Plate, 10}}, 0.5, 2.0) > tailDb({{Plate, 4}}, 0.5, 2.0) + 6.0);
    // the echo: the burst comes back one eighth note later (250 ms with no tempo given)
    auto p = make({{Echo, 10}}); std::vector<float> x(static_cast<size_t>(2 * kFs), 0.0f); for (int i = 0; i < 4800; ++i) x[static_cast<size_t>(i)] = 0.3f * static_cast<float>(std::sin(2 * kPi * 1000.0 * i / kFs));
    const auto y = run(p, x);
    CHECK(rmsDb(y, 12000 + 1200, 12000 + 3600) > -45.0);   // 250 ms after the start
    CHECK(rmsDb(y, 7200, 11000) < -80.0);                  // quiet between the burst and its echo
    // Level scales the sends along with the vocal
    const double a = tailDb({{Echo, 10}}, 0.25, 0.35), b = tailDb({{Echo, 10}, {Level, -12}}, 0.25, 0.35);
    NEAR(a - b, 12.0, 2.0);
}
TEST_CASE("VO07 the tempo sets the echo; loud input stays finite") {
    auto p = make({{Echo, 10}}); p.setTempo(60.0);   // an eighth at 60 bpm = 500 ms
    std::vector<float> x(static_cast<size_t>(2 * kFs), 0.0f); for (int i = 0; i < 4800; ++i) x[static_cast<size_t>(i)] = 0.3f * static_cast<float>(std::sin(2 * kPi * 1000.0 * i / kFs));
    const auto y = run(p, x);
    CHECK(rmsDb(y, 24000 + 1200, 24000 + 3600) > -45.0); CHECK(rmsDb(y, 12000 + 1200, 12000 + 3600) < -80.0);
    auto q = make({{Comp, 10}, {Plate, 10}, {Echo, 10}, {Deess, 10}, {Breath, 10}, {Presence, 6}, {Air, 6}, {Body, 6}, {Level, 12}});
    auto z = noise(6, 2.0, 9); for (auto& v : z) v *= 8.0f; for (float v : run(q, z)) CHECK(std::isfinite(v));
}
