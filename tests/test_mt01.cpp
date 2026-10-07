#include "doctest.h"
#include "mt01/mt01.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::mt01;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); return p; }
void feed(Processor& p, std::vector<float> l) { std::vector<float> r = l; for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } }
}

TEST_CASE("MT01 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"mt01.preset", "mt01.target", "mt01.tol", "mt01.pause"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Preset].labels.size() == 4); CHECK(s[Preset].def == 0);
    CHECK(s[Target].min == -40); CHECK(s[Target].max == -5); CHECK(s[Target].def == -24);
    CHECK(s[Tolerance].min == 0.5); CHECK(s[Tolerance].max == 3); CHECK(s[Tolerance].def == 1); CHECK_FALSE(s[Pause].automatable);
    NEAR(presetTarget(Arib, -30), -24.0, 1e-12); NEAR(presetTarget(Ebu, -30), -23.0, 1e-12); NEAR(presetTarget(Streaming, -30), -14.0, 1e-12); NEAR(presetTarget(Custom, -30), -30.0, 1e-12);
}
TEST_CASE("MT01 the signal passes unchanged; no delay") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> l = noise(-18, 2.0, 3), r = noise(-18, 2.0, 4); const auto l0 = l, r0 = r;
    for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); }
    for (size_t i = 0; i < l.size(); ++i) { CHECK(l[i] == l0[i]); CHECK(r[i] == r0[i]); }
}
TEST_CASE("MT01 a steady tone: momentary, short-term and integrated agree; the range is 0") {
    auto p = make(); feed(p, sine(-26, 12.0, 1000));
    NEAR(p.integrated(), -23.0, 0.3); NEAR(p.momentary(), p.integrated(), 0.2); NEAR(p.shortTerm(), p.integrated(), 0.2);
    CHECK(p.range() < 0.3);
}
TEST_CASE("MT01 the range of a two-level signal; the gates keep silence out") {
    auto p = make(); std::vector<float> x = sine(-40, 30.0, 1000); const auto loud = sine(-30, 30.0, 1000); x.insert(x.end(), loud.begin(), loud.end()); const auto z = std::vector<float>(static_cast<size_t>(30 * kFs), 0.0f); x.insert(x.end(), z.begin(), z.end());
    feed(p, x);
    NEAR(p.range(), 10.0, 1.5);                       // two levels 10 LU apart (the silence is gated out)
    NEAR(p.integrated(), -27.0 + 10.0 * std::log10((1.0 + 0.1) / 2.0), 0.6);   // the power mean of the two parts (-27 and -37 LUFS)
}
TEST_CASE("MT01 the true peak sees between the samples") {
    auto p = make(); std::vector<float> x(static_cast<size_t>(2 * kFs)); for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(0.5 * std::sin(2 * kPi * 12000.0 * static_cast<double>(i) / kFs + kPi / 4));   // fs/4: the samples reach 0.707 of the peak
    feed(p, x);
    NEAR(p.truePeakDb(), 20 * std::log10(0.5), 0.4);   // the sample peak would read 3 dB lower
}
TEST_CASE("MT01 presets, difference and band; Pause and reset; the history") {
    auto p = make({{Preset, Ebu}, {Tolerance, 1.0}}); feed(p, sine(-26, 8.0, 1000));   // about -23.0
    NEAR(p.target(), -23.0, 1e-9); NEAR(p.difference(), p.integrated() + 23.0, 1e-9); CHECK(p.inBand());
    p.setParam(Preset, Streaming); CHECK_FALSE(p.inBand()); NEAR(p.difference(), p.integrated() + 14.0, 1e-9);
    p.setParam(Preset, Custom); p.setParam(Target, -25.0); NEAR(p.target(), -25.0, 1e-9); CHECK_FALSE(p.inBand()); p.setParam(Tolerance, 2.5); CHECK(p.inBand());
    const double before = p.integrated(); p.setParam(Pause, 1); feed(p, sine(-10, 5.0, 1000)); NEAR(p.integrated(), before, 1e-9);   // paused
    p.setParam(Pause, 0); p.reset(); CHECK(p.integrated() < -150.0); CHECK(p.history().empty());
    feed(p, sine(-26, 5.5, 1000)); CHECK(p.history().size() == 5);
    auto q = make(); feed(q, sine(-26, 700.0, 1000)); CHECK(static_cast<int>(q.history().size()) == kHistory);   // ten minutes at most
}
TEST_CASE("MT01 silence and loud input: finite") {
    auto p = make(); feed(p, std::vector<float>(48000, 0.0f)); CHECK(p.integrated() < -150.0);
    auto x = noise(6, 2.0, 9); for (auto& v : x) v *= 8.0f; feed(p, x); CHECK(std::isfinite(p.momentary())); CHECK(std::isfinite(p.truePeakDb()));
}
