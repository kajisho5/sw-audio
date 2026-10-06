#include "doctest.h"
#include "md06/md06.hpp"
#include "sw/hilbert.hpp"
#include "tu.hpp"
#include <complex>
using namespace sw;
using namespace sw::md06;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> go1(Processor& p, std::vector<float> l) { return run(p, l); }
// frequency of the strongest partial near f0 (scan 1 Hz steps in a window)
double peakHz(const std::vector<float>& y, double lo, double hi, size_t a, size_t b) { double best = lo, bv = -1e9; for (double f = lo; f <= hi; f += 1.0) { const double v = binDb(y, f, a, b); if (v > bv) { bv = v; best = f; } } return best; }
}

TEST_CASE("MD06 the Hilbert pair: equal magnitude, q 90 degrees ahead of i") {
    for (double f : {80.0, 500.0, 3000.0, 10000.0, 18000.0}) {
        HilbertIir h; std::complex<double> ai, aq; const size_t N = 96000;
        for (size_t n = 0; n < N; ++n) {
            const double x = std::sin(2 * 3.14159265358979323846 * f * static_cast<double>(n) / kFs); double i, q; h.process(x, i, q);
            if (n >= N / 2) { const std::complex<double> e = std::exp(std::complex<double>(0, -2 * 3.14159265358979323846 * f * static_cast<double>(n) / kFs)); ai += i * e; aq += q * e; }
        }
        NEAR(20 * std::log10(std::abs(aq) / std::abs(ai)), 0.0, 0.1);
        double ph = (std::arg(aq) - std::arg(ai)) * 180.0 / 3.14159265358979323846; while (ph > 180) ph -= 360.0; while (ph < -180) ph += 360.0;
        NEAR(ph, 90.0, 1.0);   // q leads i by 90 degrees
    }
}
TEST_CASE("MD06 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"md06.shift", "md06.direction", "md06.ringmod", "md06.feedback", "md06.lfo", "md06.mix", "md06.evo.on"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Shift].min == -2000); CHECK(s[Shift].max == 2000); CHECK(s[Shift].def == 35); CHECK(s[Shift].curve == Curve::SymLog);
    NEAR(s[Shift].toValue(0.5), 0.0, 1e-9); NEAR(s[Shift].toValue(1.0), 2000.0, 1e-6); NEAR(s[Shift].toValue(0.0), -2000.0, 1e-6);
    CHECK(s[Direction].labels == std::vector<std::string>{"Up", "Down", "Both"}); CHECK(s[Direction].def == 2);
    CHECK(s[RingMod].def == 0); CHECK(s[Feedback].def == 20); CHECK(s[Lfo].def == 0); CHECK(s[Mix].def == 50); CHECK(s[PitchTrack].def == 0);
}
TEST_CASE("MD06 no delay is reported; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("MD06 a tone moves by the shift, up or down") {
    auto shifted = [&](Set set) { auto p = make(set); auto y = go1(p, sine(-12, 3.0, 1000)); return peakHz(y, 600, 2500, 48000, 144000); };
    NEAR(shifted({{Shift, 100}, {Direction, Both}, {Feedback, 0}}), 1100.0, 2.0);
    NEAR(shifted({{Shift, -100}, {Direction, Both}, {Feedback, 0}}), 900.0, 2.0);
    NEAR(shifted({{Shift, 100}, {Direction, Up}, {Feedback, 0}}), 1100.0, 2.0);
    NEAR(shifted({{Shift, -100}, {Direction, Up}, {Feedback, 0}}), 1100.0, 2.0);      // Up uses |Shift|
    NEAR(shifted({{Shift, 100}, {Direction, Down}, {Feedback, 0}}), 900.0, 2.0);
    NEAR(shifted({{Shift, -100}, {Direction, Down}, {Feedback, 0}}), 900.0, 2.0);
    NEAR(shifted({{Shift, 1000}, {Direction, Both}, {Feedback, 0}}), 2000.0, 3.0);
    // a partial shifted by +100 Hz: the opposite sideband is far below (image rejection)
    auto p = make({{Shift, 100}, {Direction, Both}, {Feedback, 0}}); auto y = go1(p, sine(-12, 3.0, 1000));
    CHECK(binDb(y, 1100, 48000, 144000) > binDb(y, 900, 48000, 144000) + 35.0);
    // the level is kept (a single sideband of a unit tone)
    NEAR(binDb(y, 1100, 48000, 144000), 20 * std::log10(std::pow(10.0, (-12 + 3.0103) / 20.0)), 0.5);
}
TEST_CASE("MD06 shift is not a pitch shift: harmonics move by the same number of Hz") {
    // 200 Hz + its 2nd and 3rd harmonics, shifted by +50 Hz: 250, 450, 650 (not 300, 500, 700 ...)
    std::vector<float> x(144000); for (size_t i = 0; i < x.size(); ++i) { const double t = static_cast<double>(i) / kFs; x[i] = static_cast<float>(0.2 * (std::sin(2 * 3.14159265358979323846 * 200 * t) + std::sin(2 * 3.14159265358979323846 * 400 * t) + std::sin(2 * 3.14159265358979323846 * 600 * t))); }
    auto p = make({{Shift, 50}, {Direction, Both}, {Feedback, 0}}); const auto y = go1(p, x);
    for (double f : {250.0, 450.0, 650.0}) CHECK(binDb(y, f, 48000, 144000) > -20.0);
    for (double f : {200.0, 400.0, 600.0}) CHECK(binDb(y, f, 48000, 144000) < -45.0);
}
TEST_CASE("MD06 Ring mod keeps both sidebands") {
    auto p = make({{Shift, 100}, {Direction, Both}, {RingMod, 1}, {Feedback, 0}}); auto y = go1(p, sine(-12, 3.0, 1000));
    const double up = binDb(y, 1100, 48000, 144000), down = binDb(y, 900, 48000, 144000);
    NEAR(up, down, 0.5); NEAR(up, 20 * std::log10(0.5 * std::pow(10.0, (-12 + 3.0103) / 20.0)), 1.0);   // each at half the amplitude
}
TEST_CASE("MD06 Feedback makes a spiral of copies, each one shift further") {
    auto p = make({{Shift, 100}, {Direction, Both}, {Feedback, 50}}); auto y = go1(p, sine(-18, 3.0, 1000));
    const double c1 = binDb(y, 1100, 48000, 144000), c2 = binDb(y, 1200, 48000, 144000), c3 = binDb(y, 1300, 48000, 144000);
    NEAR(c2 - c1, 20 * std::log10(0.5), 1.0); NEAR(c3 - c2, 20 * std::log10(0.5), 1.0);
    // 100 %: bounded with loud noise
    auto q = make({{Shift, 300}, {Feedback, 100}}); const auto z = go1(q, noise(0, 4.0, 3)); for (float v : z) { CHECK(std::isfinite(v)); CHECK(std::abs(v) < 40.0f); }
}
TEST_CASE("MD06 LFO sweeps the shift through zero") {
    auto p = make({{Shift, 200}, {Direction, Both}, {Lfo, 1}, {Feedback, 0}});
    std::vector<float> l(48000 * 8, 0.0f), r = l; double lo = 1e9, hi = -1e9;
    for (size_t off = 0; off < l.size(); off += 64) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, 64); lo = std::min(lo, p.shiftHz()); hi = std::max(hi, p.shiftHz()); }
    NEAR(hi, 200.0, 2.0); NEAR(lo, -200.0, 2.0);
}
TEST_CASE("MD06 Pitch track scales the shift with the pitch (A3 = 220 Hz is the reference)") {
    for (double f0 : {110.0, 220.0, 440.0}) {
        auto p = make({{Shift, 40}, {Direction, Both}, {PitchTrack, 1}, {Feedback, 0}}); auto y = go1(p, sine(-12, 1.5, f0));
        NEAR(p.shiftHz(), 40.0 * f0 / 220.0, 40.0 * f0 / 220.0 * 0.06);
    }
    auto off = make({{Shift, 40}, {PitchTrack, 0}}); auto y = go1(off, sine(-12, 1.0, 440)); NEAR(off.shiftHz(), 40.0, 1e-9);
}
TEST_CASE("MD06 loud noise at the extremes stays finite") {
    for (double sh : {-2000.0, 2000.0}) for (double fb : {0.0, 100.0}) for (double ring : {0.0, 1.0}) {
        auto p = make({{Shift, sh}, {Feedback, fb}, {RingMod, ring}, {Lfo, 1}});
        const auto y = go1(p, noise(0, 2.0, 4)); for (float v : y) { CHECK(std::isfinite(v)); CHECK(std::abs(v) < 60.0f); }
    }
}
