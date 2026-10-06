#include "doctest.h"
#include "md03/md03.hpp"
#include "sw/fft.hpp"
#include "sw/notes.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::md03;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}, double bpm = 0.0) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); if (bpm > 0) p.setTempo(bpm); p.snapToTargets(); return p; }
// magnitude response |H(f)| (dB) of the wet path from its impulse response; 16384 points
std::vector<double> response(Processor& p, bool plusDry) {
    std::vector<float> x(16384, 0.0f); x[0] = 1.0f; auto y = run(p, x);
    std::vector<std::complex<double>> b(16384); for (size_t i = 0; i < b.size(); ++i) b[i] = double(y[i]) + (plusDry && i == 0 ? 1.0 : 0.0);
    Fft f(16384); f.forward(b);
    std::vector<double> r(8193); for (size_t k = 0; k < r.size(); ++k) r[k] = 20 * std::log10(std::abs(b[k]) + 1e-9);
    return r;
}
double binHz(size_t k) { return k * kFs / 16384.0; }
}

TEST_CASE("MD03 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"md03.stages", "md03.rate", "md03.depth", "md03.feedback", "md03.center", "md03.mix", "md03.sync", "md03.evo.on"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Stages].labels == std::vector<std::string>{"4", "6", "8", "12"}); CHECK(s[Stages].def == 6);
    CHECK(s[Rate].min == 0.05); CHECK(s[Rate].max == 8); CHECK(s[Rate].def == 0.5); CHECK(s[Rate].curve == Curve::Log);
    CHECK(s[Depth].def == 5); CHECK(s[Feedback].def == 3); CHECK(s[Center].min == 200); CHECK(s[Center].max == 4000); CHECK(s[Center].def == 800);
    CHECK(s[Mix].def == 50); CHECK(s[Sync].def == 0); CHECK(s[NoteFollow].def == 0);
}
TEST_CASE("MD03 no delay is reported; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("MD03 the wet path is all-pass (flat magnitude) without feedback") {
    for (int st : {4, 6, 8, 12}) {
        auto p = make({{Stages, double(st)}, {Depth, 0}, {Feedback, 0}});
        const auto r = response(p, false);
        for (size_t k = 8; k < 8100; k += 37) NEAR(r[k], 0.0, 0.05);
    }
}
TEST_CASE("MD03 dry + wet has N/2 notches, the first at Center x tan(pi / 2N)") {
    for (int st : {4, 6, 8, 12}) {
        auto p = make({{Stages, double(st)}, {Depth, 0}, {Feedback, 0}, {Center, 800}});
        const auto r = response(p, true);
        int notches = 0; size_t first = 0;
        for (size_t k = 2; k + 2 < r.size(); ++k) if (r[k] < r[k - 1] && r[k] <= r[k + 1] && r[k] < -20.0) { ++notches; if (!first) first = k; }
        CHECK(notches == st / 2);
        NEAR(binHz(first) / (800.0 * std::tan(3.14159265358979323846 / (2.0 * st))), 1.0, 0.03);
    }
}
TEST_CASE("MD03 Feedback makes the response peak") {
    auto flat = make({{Depth, 0}, {Feedback, 0}}); auto fb = make({{Depth, 0}, {Feedback, 9}});
    const auto a = response(flat, false), b = response(fb, false);
    double ma = -1e9, mb = -1e9; for (size_t k = 4; k < 8100; ++k) { ma = std::max(ma, a[k]); mb = std::max(mb, b[k]); }
    NEAR(ma, 0.0, 0.05); CHECK(mb > 12.0); CHECK(mb < 16.0);   // 1 / (1 - 0.81) = 14.4 dB
}
TEST_CASE("MD03 Depth sweeps Center by +-2 octaves; the right channel runs 90 degrees behind") {
    auto p = make({{Depth, 10}, {Rate, 2.0}, {Center, 800}});
    std::vector<float> l(96000, 0.0f), r = l; double lo = 1e9, hi = 0, dot = 0; int k = 0;
    for (size_t off = 0; off < l.size(); off += 32) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, 32); lo = std::min(lo, p.sweepHz(0)); hi = std::max(hi, p.sweepHz(0)); dot += std::log2(p.sweepHz(0) / 800.0) * std::log2(p.sweepHz(1) / 800.0); ++k; }
    NEAR(hi, 3200.0, 40.0); NEAR(lo, 200.0, 5.0);
    NEAR(dot / k, 0.0, 0.2);   // 90 degrees: uncorrelated (the mean of sin x cos is 0)
}
TEST_CASE("MD03 Rate and Sync") {
    NEAR(make({{Rate, 1.3}}).rateHz(), 1.3, 1e-9);
    NEAR(make({{Rate, 2.0}, {Sync, 1}}, 120).rateHz(), 2.0, 1e-9);
    NEAR(make({{Rate, 2.0}, {Sync, 1}}, 90).rateHz(), 1.0 / noteSeconds(11, 90), 1e-9);
    NEAR(make({{Rate, 2.0}, {Sync, 1}}).rateHz(), 2.0, 1e-9);
    auto p = make({{Rate, 2.0}, {Sync, 1}, {Depth, 10}}, 120); p.setTransport(true, 0.5);
    std::vector<float> l(1, 0.0f), r = l; float* c[2] = {l.data(), r.data()}; p.process(c, 2, 1);
    // half a period before the bar line: phase 0.5 -> sin = 0 -> the centre itself
    NEAR(p.sweepHz(0), 800.0, 1.0);
}
TEST_CASE("MD03 Note follow puts the notches at the same place for every note") {
    for (double f0 : {110.0, 220.0, 330.0}) {
        auto p = make({{Depth, 0}, {Center, 800}, {NoteFollow, 1}});
        auto x = sine(-12, 1.5, f0); auto y = run(p, x);
        NEAR(p.centreHz(), 800.0 * f0 / 220.0, 800.0 * f0 / 220.0 * 0.05);
    }
    auto off = make({{Depth, 0}, {Center, 800}, {NoteFollow, 0}}); auto y = run(off, sine(-12, 1.5, 330)); NEAR(off.centreHz(), 800.0, 1e-9);
    // unvoiced input (noise): the last value is held, and a silent start stays at Center
    auto q = make({{Depth, 0}, {Center, 800}, {NoteFollow, 1}}); auto z = run(q, noise(-20, 1.0, 5)); CHECK(std::isfinite(q.centreHz()));
}
TEST_CASE("MD03 loud noise at the extremes stays finite") {
    for (int st : {4, 12}) for (double fb : {0.0, 10.0}) for (double c : {200.0, 4000.0}) {
        auto p = make({{Stages, double(st)}, {Feedback, fb}, {Center, c}, {Depth, 10}, {Rate, 8}});
        const auto y = run(p, noise(0, 2.0, 4)); for (float v : y) { CHECK(std::isfinite(v)); CHECK(std::abs(v) < 60.0f); }
    }
}
