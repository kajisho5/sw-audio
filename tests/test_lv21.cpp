#include "doctest.h"
#include "lv21/lv21.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv21;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
void start(Processor& p) { p.arm(); REQUIRE(p.outputOn()); }
}

TEST_CASE("LV21 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Signal].labels == std::vector<std::string>{"Sine", "Pink", "White", "Sweep", "Polarity"}); CHECK(s[Signal].def == 0);
    CHECK(s[Freq].min == 20); CHECK(s[Freq].max == 20000); CHECK(s[Freq].def == 1000); CHECK(s[Level].min == -60); CHECK(s[Level].max == 0); CHECK(s[Level].def == -20);
    CHECK(s[SweepTime].min == 1); CHECK(s[SweepTime].max == 60); CHECK(s[SweepTime].def == 10); CHECK(s[Left].def == 1); CHECK(s[Right].def == 1);
    CHECK(kAutoStopSeconds == 60.0); Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV21 silent until armed and started; the input passes untouched") {
    auto p = make(); const auto x = sine(-30, 1.0, 300); const auto y = run(p, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]);
    CHECK_FALSE(p.outputOn()); CHECK_FALSE(p.running());   // not armed: no output
    p.arm(); CHECK(p.armed()); CHECK_FALSE(p.running()); const auto z = run(p, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(z[i] == x[i]);   // armed only: still nothing
    CHECK(p.outputOn()); CHECK(p.running());
}
TEST_CASE("LV21 a fresh instance (a restored session) is always off") {
    auto p = make(); start(p); Processor q; q.prepare(kFs, 256); CHECK_FALSE(q.armed()); CHECK_FALSE(q.running()); p.prepare(kFs, 256); CHECK_FALSE(p.armed()); CHECK_FALSE(p.running());
}
TEST_CASE("LV21 the level comes up over 0.5 s; the sine has the Level as RMS") {
    auto p = make({{Level, -20}}); start(p); const auto y = run(p, std::vector<float>(48000 * 2, 0.0f));
    CHECK(rmsDb(y, 0, 2400) < -35.0); CHECK(std::abs(y[100]) < 0.01f);   // the first 50 ms are far under the final level
    CHECK(std::abs(rmsDb(y, 36000, 96000) - (-20.0)) < 0.2);
}
TEST_CASE("LV21 white and pink have the Level as RMS; pink falls 3 dB per octave") {
    auto w = make({{Signal, White}, {Level, -20}}); start(w); const auto y = run(w, std::vector<float>(48000 * 4, 0.0f)); CHECK(std::abs(rmsDb(y, 48000, 192000) - (-20.0)) < 0.4);
    auto p = make({{Signal, Pink}, {Level, -20}}); start(p); const auto z = run(p, std::vector<float>(48000 * 10, 0.0f)); CHECK(std::abs(rmsDb(z, 96000, 480000) - (-20.0)) < 0.8);
    // octave bands: 250 Hz vs 4 kHz, 1/3 octave bands of equal width in octaves hold equal power
    auto octave = [&](double f) { double s = 0; for (double g = f / 1.41421356; g <= f * 1.41421356; g += 2.0) s += std::pow(10.0, binDb(z, g, 96000, 192000) / 10.0); return 10 * std::log10(s * 2.0); };
    CHECK(std::abs(octave(250) - octave(4000)) < 2.5);   // equal power in every octave
}
TEST_CASE("LV21 Sweep goes 20 Hz to 20 kHz and repeats") {
    auto p = make({{Signal, Sweep}, {SweepTime, 2.0}}); start(p); const auto y = run(p, std::vector<float>(48000 * 4, 0.0f));
    // zero crossings in 50 ms windows: low at the start (1 s into the ramp-in is level, so use the log mid: sqrt(20*20000) = 632 Hz at the middle of the sweep)
    auto freqAt = [&](size_t a) { int zc = 0; for (size_t i = a + 1; i < a + 2400; ++i) if ((y[i] >= 0) != (y[i - 1] >= 0)) ++zc; return zc / 2.0 / 0.05; };
    CHECK(freqAt(46800) > 400.0); CHECK(freqAt(46800) < 900.0);   // 1 s into the 2 s sweep: the middle of the log range, sqrt(20 x 20000) = 632 Hz
    CHECK(freqAt(96000 + 2400) < 100.0);   // the second sweep starts again at the low end
    CHECK(freqAt(96000 - 4800) > 5000.0);   // just before the end of the first
}
TEST_CASE("LV21 Polarity pulse: positive 0.1 ms pulses twice a second") {
    auto p = make({{Signal, PolarityPulse}, {Level, -6}}); start(p); const auto y = run(p, std::vector<float>(48000 * 3, 0.0f));
    int pulses = 0; float peak = 0; for (size_t i = 72000; i < 144000; ++i) { if (y[i] > 0.2f && y[i - 1] <= 0.2f) ++pulses; peak = std::max(peak, y[i]); }
    CHECK(pulses == 3); CHECK(std::abs(peak - 0.5012f) < 0.01f); for (size_t i = 72000; i < 144000; ++i) REQUIRE(y[i] >= 0.0f);
}
TEST_CASE("LV21 Left / Right: the other channel keeps the input") {
    auto p = make({{Right, 0}, {Level, -20}}); start(p); std::vector<float> l(48000 * 2, 0.0f), r(48000 * 2, 0.25f); const auto o = run2(p, l, r);
    CHECK(rmsDb(o.first, 60000, 96000) > -22.0); for (size_t i = 0; i < r.size(); ++i) REQUIRE(o.second[i] == 0.25f);
}
TEST_CASE("LV21 stops by itself after 60 s; disarm stops at once with a fade") {
    auto p = make(); start(p); run(p, std::vector<float>(static_cast<size_t>(48000 * 61.0), 0.0f)); CHECK_FALSE(p.running()); CHECK_FALSE(p.armed());
    auto q = make(); start(q); run(q, std::vector<float>(48000, 0.0f)); q.disarm(); CHECK_FALSE(q.running()); const auto y = run(q, std::vector<float>(4800, 0.0f)); CHECK(std::abs(y[2000]) < 1e-3f);
}
TEST_CASE("LV21 mono, odd blocks, before prepare") {
    auto p = make(); start(p); std::vector<float> l(48000, 0.0f); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; p.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f); CHECK_FALSE(z.outputOn());
}
