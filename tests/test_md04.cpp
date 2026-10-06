#include "doctest.h"
#include "md04/md04.hpp"
#include "sw/notes.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::md04;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}, double bpm = 0.0) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); if (bpm > 0) p.setTempo(bpm); p.snapToTargets(); return p; }
void go(Processor& p, std::vector<float>& l, std::vector<float>& r) { for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } }
// envelope of a (loud) tone in short windows
std::vector<double> envelope(const std::vector<float>& y, size_t win, size_t from = 0) { std::vector<double> e; for (size_t a = from; a + win <= y.size(); a += win) { double m = 0; for (size_t i = a; i < a + win; ++i) m = std::max(m, double(std::abs(y[i]))); e.push_back(m); } return e; }
}

TEST_CASE("MD04 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"md04.mode", "md04.rate", "md04.depth", "md04.shape", "md04.width", "md04.sync"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Mode].labels == std::vector<std::string>{"Tremolo", "Auto pan", "Harmonic"}); CHECK(s[Mode].def == 1);
    CHECK(s[Rate].min == 0.1); CHECK(s[Rate].max == 20); CHECK(s[Rate].def == 4); CHECK(s[Rate].curve == Curve::Log);
    CHECK(s[Depth].def == 60); CHECK(s[Shape].labels == std::vector<std::string>{"Sine", "Triangle", "Square", "Ramp"}); CHECK(s[Shape].def == 0);
    CHECK(s[Width].def == 100); CHECK(s[Sync].def == 1);
}
TEST_CASE("MD04 no delay is reported; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    for (int mode = 0; mode < 3; ++mode) { auto p = make({{Mode, double(mode)}}); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f); }
}
TEST_CASE("MD04 the LFO shapes") {
    for (double ph : {0.0, 0.1, 0.25, 0.5, 0.75, 0.9}) {
        NEAR(lfoValue(Sine, ph), std::sin(2 * 3.14159265358979323846 * ph), 1e-12);
        CHECK(std::abs(lfoValue(Triangle, ph)) <= 1.0 + 1e-12); CHECK(std::abs(lfoValue(Square, ph)) == 1.0); CHECK(std::abs(lfoValue(Ramp, ph)) <= 1.0);
    }
    NEAR(lfoValue(Triangle, 0.25), 0.0, 1e-12); NEAR(lfoValue(Triangle, 0.5), 1.0, 1e-12); NEAR(lfoValue(Triangle, 0.0), -1.0, 1e-12);
    NEAR(lfoValue(Ramp, 0.0), -1.0, 1e-12); NEAR(lfoValue(Ramp, 0.5), 0.0, 1e-12); NEAR(lfoValue(Ramp, 0.999), 0.998, 1e-9);
    CHECK(lfoValue(Square, 0.25) == 1.0); CHECK(lfoValue(Square, 0.75) == -1.0);
}
TEST_CASE("MD04 Tremolo: the gain goes down to 1 - Depth") {
    for (double depth : {30.0, 60.0, 100.0}) for (int shape : {Sine, Triangle}) {
        auto p = make({{Mode, Tremolo}, {Depth, depth}, {Rate, 2.0}, {Shape, double(shape)}, {Sync, 0}, {Width, 0}});
        auto l = sine(-6, 4.0, 1000), r = l; go(p, l, r);
        const auto e = envelope(l, 48, 24000); double lo = 1e9, hi = 0; for (double v : e) { lo = std::min(lo, v); hi = std::max(hi, v); }
        const double a = std::pow(10.0, (-6 + 3.0103) / 20.0);   // the tone's amplitude
        NEAR(hi / a, 1.0, 0.03); NEAR(lo / a, 1.0 - depth * 0.01, 0.03);
    }
}
TEST_CASE("MD04 Rate is the LFO speed; Sync reads the note at the host tempo") {
    NEAR(make({{Rate, 7.0}, {Sync, 0}}).rateHz(), 7.0, 1e-9);
    NEAR(make({{Rate, 4.0}, {Sync, 1}}, 120).rateHz(), 4.0, 1e-9);                           // the default: 1/8 at 120 bpm
    NEAR(make({{Rate, 4.0}, {Sync, 1}}, 90).rateHz(), 1.0 / noteSeconds(8, 90), 1e-9);        // 1/8 at 90 bpm
    NEAR(make({{Rate, 4.0}, {Sync, 1}}).rateHz(), 4.0, 1e-9);                                 // no tempo: Hz
    auto p = make({{Mode, Tremolo}, {Rate, 3.0}, {Sync, 0}, {Depth, 100}});
    std::vector<float> l(48000 * 10, 0.0f), r = l; for (auto& v : l) v = 0.5f; r = l; int ups = 0; double prev = 0;
    for (size_t off = 0; off < l.size(); off += 64) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, 64); const double d = p.lfo(0); if (prev < 0 && d >= 0) ++ups; prev = d; }
    NEAR(ups / 10.0, 3.0, 0.11);
}
TEST_CASE("MD04 Square and Ramp: two levels / one jump per cycle, edges smoothed (2 ms time constant)") {
    auto sq = make({{Mode, Tremolo}, {Shape, Square}, {Depth, 100}, {Rate, 2.0}, {Sync, 0}, {Width, 0}});
    std::vector<float> l(48000 * 3, 0.5f), r = l; go(sq, l, r);
    int low = 0, high = 0, mid = 0; for (size_t i = 24000; i < l.size(); ++i) { if (l[i] < 0.05f) ++low; else if (l[i] > 0.45f) ++high; else ++mid; }
    CHECK(low > 0.45 * 72000); CHECK(high > 0.45 * 72000); CHECK(mid < 0.05 * 72000);   // the edges take a few ms (a 2 ms time constant)
    double maxStep = 0; for (size_t i = 24001; i < l.size(); ++i) maxStep = std::max(maxStep, double(std::abs(l[i] - l[i - 1])));
    CHECK(maxStep < 0.5 * 0.2);   // an edge of 0.5 spread over 2 ms (96 samples): the biggest step is far below the jump
    auto rp = make({{Mode, Tremolo}, {Shape, Ramp}, {Depth, 100}, {Rate, 2.0}, {Sync, 0}, {Width, 0}});
    std::vector<float> m(48000 * 3, 0.5f), n = m; go(rp, m, n);
    int up = 0, down = 0; for (size_t i = 24001; i < m.size(); ++i) { const double d = m[i] - m[i - 1]; if (d > 1e-7) ++up; else if (d < -1e-7) ++down; }
    CHECK(up > 0.9 * 72000 * 0.9); CHECK(down < 0.1 * 72000);   // up slowly for most of the cycle, down only in the quick drop
}
TEST_CASE("MD04 Auto pan: constant power, centre at 1, Width scales the excursion") {
    auto p = make({{Mode, AutoPan}, {Depth, 100}, {Width, 100}, {Rate, 1.0}, {Sync, 0}});
    std::vector<float> l(48000 * 3, 0.5f), r = l; go(p, l, r);
    double minL = 1e9, maxL = 0, minR = 1e9, maxR = 0;
    for (size_t i = 24000; i < l.size(); ++i) {
        NEAR(l[i] * l[i] + r[i] * r[i], 0.5, 1e-5);   // (0.5 sqrt2 cos)^2 + (0.5 sqrt2 sin)^2 = 0.5
        minL = std::min(minL, double(l[i])); maxL = std::max(maxL, double(l[i])); minR = std::min(minR, double(r[i])); maxR = std::max(maxR, double(r[i]));
    }
    CHECK(minL < 0.01); CHECK(maxL > 0.69); CHECK(minR < 0.01); CHECK(maxR > 0.69);      // hard left / hard right (x sqrt2 at the edge)
    auto q = make({{Mode, AutoPan}, {Depth, 100}, {Width, 0}, {Rate, 1.0}, {Sync, 0}});
    std::vector<float> a(48000, 0.5f), b = a; go(q, a, b); for (size_t i = 0; i < a.size(); ++i) { NEAR(a[i], 0.5, 1e-6); NEAR(b[i], 0.5, 1e-6); }
    auto h = make({{Mode, AutoPan}, {Depth, 50}, {Width, 50}, {Rate, 1.0}, {Sync, 0}});
    std::vector<float> c(48000 * 2, 0.5f), d = c; go(h, c, d); double mx = 0; for (size_t i = 0; i < c.size(); ++i) mx = std::max(mx, double(c[i]));
    NEAR(mx, 0.5 * std::sqrt(2.0) * std::cos(3.14159265358979323846 / 4 * (1 - 0.25)), 0.01);
}
TEST_CASE("MD04 Harmonic: low and high move in opposite phases") {
    auto lowT = sine(-12, 4.0, 200), highT = sine(-12, 4.0, 3000);
    auto p = make({{Mode, Harmonic}, {Depth, 80}, {Rate, 1.0}, {Sync, 0}, {Width, 0}}); auto q = make({{Mode, Harmonic}, {Depth, 80}, {Rate, 1.0}, {Sync, 0}, {Width, 0}});
    auto l1 = lowT, r1 = lowT, l2 = highT, r2 = highT; go(p, l1, r1); go(q, l2, r2);
    const auto e1 = envelope(l1, 480, 48000), e2 = envelope(l2, 480, 48000);
    const double a = std::pow(10.0, (-12 + 3.0103) / 20.0);
    double sum = 0, mn = 1e9, mx = 0, dot = 0, m1 = 0, m2 = 0;
    for (size_t i = 0; i < e1.size(); ++i) { m1 += e1[i]; m2 += e2[i]; }
    m1 /= e1.size(); m2 /= e2.size();
    for (size_t i = 0; i < e1.size(); ++i) { sum = (e1[i] + e2[i]) / a; mn = std::min(mn, sum); mx = std::max(mx, sum); dot += (e1[i] - m1) * (e2[i] - m2); }
    CHECK(dot < 0);                       // opposite phases
    NEAR(mn, 2.0 - 0.8, 0.12); NEAR(mx, 2.0 - 0.8, 0.12);   // the two gains always add up to 2 - Depth
    double lo1 = 1e9, hi1 = 0; for (double v : e1) { lo1 = std::min(lo1, v / a); hi1 = std::max(hi1, v / a); }
    NEAR(lo1, 0.2, 0.06); NEAR(hi1, 1.0, 0.05);
}
TEST_CASE("MD04 Tremolo Width: the right channel lags by up to 180 degrees") {
    auto p = make({{Mode, Tremolo}, {Width, 100}, {Depth, 100}, {Rate, 2.0}, {Sync, 0}});
    std::vector<float> l(48000, 0.5f), r = l; go(p, l, r);
    for (size_t i = 12000; i < l.size(); i += 97) NEAR(l[i] + r[i], 0.5, 0.02);   // opposite phases at depth 100 with a sine: u and -u add up to 1 x amplitude
}
TEST_CASE("MD04 on the bar grid") {
    auto p = make({{Rate, 2.0}, {Sync, 1}, {Mode, Tremolo}}, 120); p.setTransport(true, 0.5);
    std::vector<float> l(1, 0.5f), r = l; float* c[2] = {l.data(), r.data()}; p.process(c, 2, 1);
    NEAR(p.lfo(0), std::sin(2 * 3.14159265358979323846 * (0.5 + 2.0 / kFs)), 1e-9);
}
