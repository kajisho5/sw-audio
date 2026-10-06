#include "doctest.h"
#include "gt04/gt04.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::gt04;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> tone(double rmsDbfs, double f, double sec) { return sine(rmsDbfs, sec, f); }
double at(const std::vector<float>& y, double f) { return binDb(y, f, y.size() / 2, y.size()); }
double thd(const std::vector<float>& y, double f) { double e = 0; for (int k = 2; k <= 8; ++k) e += std::pow(10.0, at(y, f * k) / 10.0); return 10 * std::log10(e + 1e-30) - at(y, f); }
// the amp alone: DI off, Drive 0, everything flat
Set amp() { return {{Gain, 5}, {Drive, 0}, {Master, 5}, {Low, 5}, {LoMid, 5}, {HiMid, 5}, {High, 5}, {MidHz, 800}, {Di, 0}, {PhaseAlign, 1}}; }
Set with(Set a, Set b) { a.insert(a.end(), b.begin(), b.end()); return a; }
double gainAt(Set s, double f, double lvl = -40) { auto p = make(s); const auto x = tone(lvl, f, 1.0); return at(run(p, x), f) - at(x, f); }
// phase of y relative to x at f (radians), from the last second
double phaseAt(const std::vector<float>& y, const std::vector<float>& x, double f) {
    std::complex<double> a, b; for (size_t i = x.size() / 2; i < x.size(); ++i) { const auto e = std::exp(std::complex<double>(0, -2 * kPi * f * static_cast<double>(i) / kFs)); a += double(y[i]) * e; b += double(x[i]) * e; }
    return std::arg(a / b);
}
}

TEST_CASE("GT04 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"gt04.gain", "gt04.drive", "gt04.master", "gt04.low", "gt04.lomid", "gt04.himid", "gt04.high", "gt04.midhz", "gt04.di", "gt04.diblend", "gt04.evo.on"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Gain].def == 5); CHECK(s[Drive].def == 0); CHECK(s[Master].def == 5);
    for (int i : {Low, LoMid, HiMid, High}) { CHECK(s[static_cast<size_t>(i)].min == 0); CHECK(s[static_cast<size_t>(i)].max == 10); CHECK(s[static_cast<size_t>(i)].def == 5); }
    CHECK(s[MidHz].steps == std::vector<double>{250, 500, 800, 1500, 3000}); CHECK(s[MidHz].labels == std::vector<std::string>{"250", "500", "800", "1.5k", "3k"}); CHECK(s[MidHz].def == 800);
    CHECK(s[Di].def == 1); CHECK(s[DiBlend].def == 50); CHECK(s[DiBlend].max == 100);
}
TEST_CASE("GT04 no delay; silence and DC") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(amp()); std::vector<float> z(24000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
    auto r = make(amp()); std::vector<float> dc(96000, 0.2f); CHECK(peakDb(run(r, dc), 72000, 96000) < -50.0);
}
TEST_CASE("GT04 flat settings are roughly flat from 100 Hz to 3 kHz; the cabinet takes the top and the bottom") {
    for (double f : {100.0, 200.0, 400.0, 800.0, 1600.0, 3000.0}) NEAR(gainAt(amp(), f), 0.0, 3.5);
    CHECK(gainAt(amp(), 25.0) < -6.0); CHECK(gainAt(amp(), 12000.0) < -12.0);
}
TEST_CASE("GT04 each EQ band moves its own region by about +-12 dB") {
    const double base60 = gainAt(amp(), 60), base400 = gainAt(amp(), 400), base800 = gainAt(amp(), 800), base8k = gainAt(amp(), 8000);
    CHECK(gainAt(with(amp(), {{Low, 10}}), 60) - base60 > 8.0); CHECK(gainAt(with(amp(), {{Low, 0}}), 60) - base60 < -8.0);
    CHECK(gainAt(with(amp(), {{LoMid, 10}}), 400) - base400 > 8.0); CHECK(gainAt(with(amp(), {{LoMid, 0}}), 400) - base400 < -8.0);
    CHECK(gainAt(with(amp(), {{HiMid, 10}}), 800) - base800 > 8.0); CHECK(gainAt(with(amp(), {{HiMid, 0}}), 800) - base800 < -8.0);
    CHECK(gainAt(with(amp(), {{High, 10}}), 8000) - base8k > 6.0); CHECK(gainAt(with(amp(), {{High, 0}}), 8000) - base8k < -6.0);
    NEAR(gainAt(with(amp(), {{Low, 10}}), 3000), gainAt(amp(), 3000), 1.0);   // the low shelf leaves the mids alone
}
TEST_CASE("GT04 Mid Hz moves the mid bells") {
    const Set boost = {{HiMid, 10}, {LoMid, 5}};
    const double at800 = gainAt(with(amp(), boost), 800) - gainAt(amp(), 800), at1500 = gainAt(with(amp(), boost), 1500) - gainAt(amp(), 1500);
    CHECK(at800 > 8.0); CHECK(at1500 < 6.0);
    const Set to1500 = with(boost, {{MidHz, 1500}});
    CHECK(gainAt(with(amp(), to1500), 1500) - gainAt(amp(), 1500) > 8.0);
    const Set lo = {{LoMid, 10}, {MidHz, 1500}};   // Lo mid sits an octave below
    CHECK(gainAt(with(amp(), lo), 750) - gainAt(amp(), 750) > 8.0);
}
TEST_CASE("GT04 Gain and Drive add harmonics; Drive leaves the lows clean") {
    const auto x = tone(-24, 400, 1.0);
    auto g2 = make(with(amp(), {{Gain, 2}})); auto g10 = make(with(amp(), {{Gain, 10}}));
    CHECK(thd(run(g10, x), 400) > thd(run(g2, x), 400) + 6.0);
    auto d0 = make(amp()); auto d10 = make(with(amp(), {{Drive, 10}}));
    CHECK(thd(run(d10, x), 400) > thd(run(d0, x), 400) + 15.0);
    const auto lo = tone(-24, 60, 1.0);
    auto l0 = make(amp()); auto l10 = make(with(amp(), {{Drive, 10}}));
    CHECK(thd(run(l10, lo), 60) < thd(run(l0, lo), 60) + 8.0);
}
TEST_CASE("GT04 Master is the amp's volume; the DI does not follow it") {
    const auto x = tone(-30, 400, 1.0);
    auto m0 = make(with(amp(), {{Master, 0}})); auto m5 = make(amp()); auto m10 = make(with(amp(), {{Master, 10}}));
    const double l0 = rmsDb(run(m0, x)), l5 = rmsDb(run(m5, x)), l10 = rmsDb(run(m10, x));
    CHECK(l0 < l5 - 40.0); CHECK(l10 > l5 + 6.0);
    auto di = make(with(amp(), {{Di, 1}, {DiBlend, 100}, {Master, 0}})); const auto y = run(di, x);
    NEAR(rmsDb(y), rmsDb(x), 0.3);
}
TEST_CASE("GT04 DI: blend 100 % is the input, 0 % is the amp, Off ignores the blend") {
    const auto x = tone(-30, 300, 1.0);
    auto only = make(with(amp(), {{Di, 1}, {DiBlend, 100}, {PhaseAlign, 0}})); const auto yd = run(only, x);
    for (size_t i = 24000; i < x.size(); ++i) CHECK(std::abs(yd[i] - x[i]) < 3e-3);
    auto a = make(with(amp(), {{Di, 1}, {DiBlend, 0}})); auto b = make(amp()); CHECK(run(a, x) == run(b, x));
    auto off = make(with(amp(), {{Di, 0}, {DiBlend, 100}})); auto b2 = make(amp()); CHECK(run(off, x) == run(b2, x));
}
TEST_CASE("GT04 Phase align brings the DI in step with the amp") {
    const Set base = with(amp(), {{Di, 1}, {DiBlend, 100}});
    const Set ampOnly = with(amp(), {{Di, 0}});
    for (double f : {100.0, 150.0}) {
        const auto x = tone(-40, f, 1.0);
        auto am = make(ampOnly); const auto ya = run(am, x);
        auto on = make(with(base, {{PhaseAlign, 1}})); auto off = make(with(base, {{PhaseAlign, 0}}));
        const double pa = phaseAt(ya, x, f), pon = phaseAt(run(on, x), x, f), poff = phaseAt(run(off, x), x, f);
        const double dOn = std::abs(std::remainder(pon - pa, 2 * kPi)), dOff = std::abs(std::remainder(poff - pa, 2 * kPi));
        CHECK(dOn < 0.25); CHECK(dOff > 2.0 * dOn);   // exact at 150 Hz (the probe), 0.19 rad away at 100 Hz
    }
}
TEST_CASE("GT04 the channels are independent and the EQ settings can move without blowing up") {
    auto p = make(with(amp(), {{Drive, 6}})); const auto l = tone(-20, 200, 0.5); std::vector<float> sil(l.size(), 0.0f);
    const auto o = run2(p, l, sil); for (float v : o.second) CHECK(v == 0.0f);
    auto q = make(amp()); for (int k = 0; k < 20; ++k) { q.setParam(Low, k % 11); q.setParam(MidHz, k % 2 ? 500 : 3000); for (float v : run(q, noise(-20, 0.05))) CHECK(std::isfinite(v)); }
}
