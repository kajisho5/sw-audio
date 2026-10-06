#include "doctest.h"
#include "md01/md01.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::md01;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
void go(Processor& p, std::vector<float>& l, std::vector<float>& r) { for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } }
}

TEST_CASE("MD01 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"md01.mode", "md01.rate", "md01.depth", "md01.width", "md01.tone", "md01.mix"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Mode].labels == std::vector<std::string>{"I", "II", "I+II"}); CHECK(s[Mode].def == 1);
    CHECK(s[Rate].min == 0.1); CHECK(s[Rate].max == 5); CHECK(s[Rate].def == 0.5); CHECK(s[Rate].curve == Curve::Log);
    CHECK(s[Depth].def == 5); CHECK(s[Depth].max == 10); CHECK(s[Width].def == 100); CHECK(s[Tone].def == 50); CHECK(s[Mix].def == 50);
}
TEST_CASE("MD01 no delay is reported; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("MD01 the delay centres on 7 ms and moves by Depth") {
    for (double depth : {0.0, 5.0, 10.0}) for (int mode = 0; mode < 3; ++mode) {
        auto p = make({{Mode, double(mode)}, {Depth, depth}, {Rate, 2.0}});
        std::vector<float> l(48000 * 2, 0.0f), r = l; double lo = 1e9, hi = -1e9;
        for (size_t off = 0; off < l.size(); off += 256) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, 256); const double d = p.tapDelaySamples(0, mode == I ? 0 : 1) * 1000.0 / kFs; lo = std::min(lo, d); hi = std::max(hi, d); }
        const double dev = depth * 0.1 * 3.0 * (mode == I ? 0.6 : 1.0);
        NEAR(hi - lo, 2.0 * dev, 0.1 + 0.03 * dev);
        NEAR(0.5 * (hi + lo), 7.0, 0.1);
    }
}
TEST_CASE("MD01 Rate sets the speed of the sweep") {
    auto period = [&](double rate) {
        auto p = make({{Mode, I}, {Depth, 10}, {Rate, rate}});
        std::vector<float> l(static_cast<size_t>(48000 * 10), 0.0f), r = l; std::vector<double> d;
        for (size_t off = 0; off < l.size(); off += 64) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, 64); d.push_back(p.tapDelaySamples(0, 0)); }
        int ups = 0; for (size_t i = 1; i < d.size(); ++i) if (d[i - 1] < 7.0 * 48 && d[i] >= 7.0 * 48) ++ups;   // upward crossings of the centre
        return ups / 10.0;   // Hz
    };
    NEAR(period(0.5), 0.5, 0.11); NEAR(period(2.0), 2.0, 0.11); NEAR(period(5.0), 5.0, 0.11);
}
TEST_CASE("MD01 Width: the right channel's sweep runs 0 .. 180 degrees against the left's") {
    for (double w : {0.0, 50.0, 100.0}) {
        auto p = make({{Mode, I}, {Depth, 10}, {Rate, 1.0}, {Width, w}});
        std::vector<float> l(48000 * 2, 0.0f), r = l; double sumErr = 0; int k = 0;
        for (size_t off = 0; off < l.size(); off += 64) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, 64); const double dl = p.tapDelaySamples(0, 0) - 7.0 * 48, dr = p.tapDelaySamples(1, 0) - 7.0 * 48; sumErr += dl * dr; ++k; }
        const double corr = sumErr / k / (3.0 * 0.6 * 48 * 3.0 * 0.6 * 48 / 3.0);   // normalised by the triangle's mean square (dev^2 / 3)
        if (w == 0.0) NEAR(corr, 1.0, 0.1);
        if (w == 50.0) NEAR(corr, 0.0, 0.15);
        if (w == 100.0) NEAR(corr, -1.0, 0.1);
    }
}
TEST_CASE("MD01 at Wide the wobble cancels in the mono sum") {
    auto sidebands = [&](double width, bool mono) {
        auto p = make({{Mode, I}, {Depth, 10}, {Rate, 5.0}, {Width, width}, {Tone, 100}});
        auto l = sine(-12, 4.0, 1000), r = l; go(p, l, r);
        std::vector<float> m(l.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = mono ? 0.5f * (l[i] + r[i]) : l[i];
        return std::max(binDb(m, 1005, 48000, 192000), binDb(m, 995, 48000, 192000)) - binDb(m, 1000, 48000, 192000);
    };
    const double left = sidebands(100, false), mono = sidebands(100, true), narrow = sidebands(0, true);
    CHECK(left > -30.0);               // one channel does move (a sideband at 5 Hz away)
    CHECK(mono < left - 20.0);          // the first-order wobble cancels in L + R
    CHECK(narrow > mono + 15.0);        // Width 0 (Mono) keeps it
}
TEST_CASE("MD01 Tone sets the band limit; the hiss only exists with signal") {
    auto hi = [&](double tone) { auto p = make({{Tone, tone}, {Depth, 0}}); auto l = sine(-12, 1.0, 9000), r = l; go(p, l, r); return rmsDb(l, 24000, 48000); };
    CHECK(hi(100) > hi(0) + 12.0);
    auto p = make({{Depth, 0}}); auto l = noise(-60, 1.0, 3); l.resize(96000, 0.0f); auto r = l; go(p, l, r);
    CHECK(rmsDb(l, 24000, 48000) > -70.0);        // the signal (and a little hiss) there
    CHECK(rmsDb(l, 80000, 96000) < -120.0);       // later: silence in, silence out (the delay is 7 ms)
}
TEST_CASE("MD01 the modes differ and all stay finite") {
    auto x = noise(-12, 2.0, 7);
    std::vector<std::vector<float>> out;
    for (int mode = 0; mode < 3; ++mode) { auto p = make({{Mode, double(mode)}, {Depth, 10}, {Rate, 5.0}}); auto l = x, r = x; go(p, l, r); for (float v : l) CHECK(std::isfinite(v)); out.push_back(l); }
    double d01 = 0, d02 = 0; for (size_t i = 24000; i < out[0].size(); ++i) { d01 += std::abs(out[0][i] - out[1][i]); d02 += std::abs(out[0][i] - out[2][i]); }
    CHECK(d01 > 1.0); CHECK(d02 > 1.0);
}
