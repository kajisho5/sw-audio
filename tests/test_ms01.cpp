#include "doctest.h"
#include "ms01/ms01.hpp"
#include "sw/loudness.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::ms01;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// drum-like loop: decaying 90 Hz hits + a little noise, every 0.25 s
std::vector<float> loop(double peak, double seconds) {
    std::vector<float> x(static_cast<size_t>(seconds * kFs), 0.0f); Gauss nz(5);
    for (size_t i = 0; i < x.size(); ++i) { const double t = static_cast<double>(i % 12000) / kFs; x[i] = static_cast<float>(peak * std::exp(-t * 18) * (std::sin(2 * kPi * 90 * t) + 0.15 * nz.gauss())); }
    return x;
}
double crestDb(const std::vector<float>& y, size_t from) { return peakDb(y, from, y.size()) - rmsDb(y, from, y.size()); }
}

TEST_CASE("MS01 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"ms01.gain", "ms01.target", "ms01.evo.on", "ms01.char.x", "ms01.char.y", "ms01.ceiling", "ms01.release", "ms01.stereo", "ms01.tp", "ms01.dither", "ms01.lowguard"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Gain].min == 0); CHECK(s[Gain].max == 24); CHECK(s[Gain].def == 0);
    CHECK(s[Target].min == -30); CHECK(s[Target].max == -5); CHECK(s[Target].def == -14.0);
    CHECK(s[Lock].def == 0); CHECK(s[CharX].def == 50); CHECK(s[CharY].def == 50);
    CHECK(s[Ceiling].min == -12); CHECK(s[Ceiling].max == 0); CHECK(s[Ceiling].def == -1.0);
    CHECK(s[Release].min == 1); CHECK(s[Release].max == 1000); CHECK(s[Release].skew == 3); CHECK(std::string(s[Release].maxLabel) == "Auto"); CHECK(s[Release].def == 1000);
    CHECK(s[Stereo].def == 100); CHECK(s[TruePeak].def == 1); CHECK(s[Dither].steps == std::vector<double>{0, 16, 24}); CHECK(s[LowGuard].def == 0);
}
TEST_CASE("MS01 latency: 2 ms look-ahead (96) + the true-peak interpolation (16) = 112 @48 kHz") {
    Processor p; CHECK(p.latencySamples() == 112);
    p.setParam(TruePeak, 0); CHECK(p.latencySamples() == 96);
}
TEST_CASE("MS01 Gain lifts the level and the ceiling holds") {
    auto p = make({{Gain, 12}, {Ceiling, 0}, {CharX, 0}});
    NEAR(rmsDb(run(p, sine(-30, 2))), -18.0, 0.3);
    auto q = make({{Gain, 24}});
    const auto y = run(q, loop(0.5, 4));
    CHECK(peakDb(y, 24000, y.size()) <= -1.0 + 0.05);
    NEAR(peakDb(y, 24000, y.size()), -1.0, 0.5);   // pushed right up to it
}
TEST_CASE("MS01 Character X (Clean..Dense): the slow stage takes work off the fast limiter; Y (Smooth..Punch) sets how late it grabs") {
    auto work = [](double x) {   // mean gain reduction of the fast limiter over a loud loop
        auto p = make({{Gain, 18}, {CharX, x}}); const auto in = loop(0.5, 5); double sum = 0; int n = 0;
        for (size_t off = 0; off + 256 <= in.size(); off += 256) { std::vector<float> a(in.begin() + off, in.begin() + off + 256), b = a; float* c[2] = {a.data(), b.data()}; p.process(c, 2, 256); if (off > 48000) { sum += p.fastReductionDb(); ++n; } }
        return sum / n;
    };
    CHECK(work(100) > work(0) + 0.5);   // less (negative) reduction left for the fast stage
    auto slowGr = [](double y) {        // the slow stage 2 ms into a loud hit after a quiet bed
        auto p = make({{Gain, 12}, {CharY, y}, {CharX, 100}});
        std::vector<float> x(24000 + 96, 0.0f); const auto bed = sine(-40, 0.5, 200), hit = sine(-3, 0.002, 200);
        std::copy(bed.begin(), bed.begin() + 24000, x.begin()); std::copy(hit.begin(), hit.end(), x.begin() + 24000);
        run(p, x); return p.slowReductionDb();
    };
    CHECK(slowGr(0) < slowGr(100) - 2.0);   // Smooth is already down, Punch has not got there yet
}
TEST_CASE("MS01 Low end guard keeps bass from driving the slow stage") {
    auto gr = [](int guard) { auto p = make({{Gain, 12}, {CharX, 100}, {LowGuard, static_cast<double>(guard)}, {Release, 1}}); auto x = sine(-12, 3, 60); const auto hi = sine(-30, 3, 3000); for (size_t i = 0; i < x.size(); ++i) x[i] += hi[i]; run(p, x); return p.slowReductionDb(); };
    CHECK(gr(1) > gr(0) + 1.0);   // less (negative) reduction
}
TEST_CASE("MS01 Stereo link: 0 % leaves the quiet side alone, 100 % pulls it down with the loud one") {
    auto right = [](double link) { auto p = make({{Gain, 12}, {Stereo, link}, {CharX, 0}}); auto [l, r] = run2(p, sine(-6, 2, 100), sine(-30, 2, 700)); return rmsDb(r); };
    NEAR(right(0), -18.0, 0.5);
    CHECK(right(100) < right(0) - 3.0);
}
TEST_CASE("MS01 Dither 16 puts the output on the 16-bit grid") {
    auto p = make({{Dither, 16}});
    for (float v : run(p, sine(-20, 0.2))) { const double q = v * 32768.0; REQUIRE(std::abs(q - std::round(q)) < 1e-3); }
}
TEST_CASE("MS01 Lock reaches Target by itself and then holds the gain") {
    auto p = make({{Lock, 1}, {Target, -14}});
    const auto in = noise(-26, 60, 9);
    const auto y = run(p, in);
    CHECK(p.locked());
    const double g = p.gainDb(); CHECK(g > 5.0); CHECK(g < 24.0);
    // the output of the last 10 s sits at the target
    IntegratedLoudness m; m.setup(48000.0, 2, 0.0);
    for (size_t off = y.size() - 480000; off + 480 <= y.size(); off += 480) { const float* c[2] = {y.data() + off, y.data() + off}; m.process(c, 2, 480); }
    NEAR(m.integrated(), -14.0, 0.8);
    // once locked the gain stays put, and the same input gives the same result
    run(p, noise(-26, 5, 3)); NEAR(p.gainDb(), g, 1e-9);
    auto q = make({{Lock, 1}, {Target, -14}}); run(q, in); NEAR(q.gainDb(), g, 1e-6);
}
TEST_CASE("MS01 Lock Off leaves Gain manual; silence and extreme input are safe") {
    auto p = make({{Gain, 6}}); run(p, sine(-30, 2)); NEAR(p.gainDb(), 6.0, 1e-9); CHECK_FALSE(p.locked());
    { auto z = make(); for (float v : run(z, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f); }
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : run(p, x)) REQUIRE(std::isfinite(v));
}
