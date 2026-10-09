// Unit A / B / C in the products (spec common function): the gain tolerance is the Shell's (test_shell.cpp); here the frequency and saturation-onset tolerances of the cores.
//   Unit A: the left and the right channel are exactly alike. Unit B / C: they differ by a small, fixed amount (a part of a dB at the slopes of the filters, the saturation a little earlier on one side).
#include "doctest.h"
#include "eq_helpers.hpp"
#include "unit_helpers.hpp"
#include "eq01/eq01.hpp"
#include "eq03/eq03.hpp"
#include "eq04/eq04.hpp"
#include "eq05/eq05.hpp"
#include "eq06/eq06.hpp"
#include "eq09/eq09.hpp"
#include "dy01/dy01.hpp"
#include "dy06/dy06.hpp"
#include "cs01/cs01.hpp"
#include "cs03/cs03.hpp"
#include "gt01/gt01.hpp"
#include "gt04/gt04.hpp"
#include "sa01/sa01.hpp"
#include "sa02/sa02.hpp"
#include "sa03/sa03.hpp"
#include "sa04/sa04.hpp"
#include "sw/unit.hpp"
#include <cmath>
#include <vector>
using namespace sw;

namespace {
// the L / R spread (dB) of the filter slopes: Unit A must give exactly 0, Unit B / C a part of a dB (three per cent of frequency on a 6 dB/oct slope is about 0.26 dB)
template <class P, class Setup> double slopeSpread(int unit, Setup setup, std::initializer_list<double> freqs, double amp = 0.01) {
    return unt::maxLRDiff([&] { P p; setup(p, unit); p.prepare(unt::kFs, 256); p.snapToTargets(); return p; }, freqs, amp);
}
// the 3rd harmonic (dB re the fundamental) of the left and the right channel for the same loud sine: where the saturation sets in
template <class P, class Setup> std::pair<double, double> harmonicLR(int unit, Setup setup, double f, double amp) {
    P p; setup(p, unit); p.prepare(unt::kFs, 256); p.snapToTargets();
    const int n = 48000; std::vector<float> l(n), r(n);
    for (int i = 0; i < n; ++i) l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] = static_cast<float>(amp * std::sin(2 * unt::kPi * f * i / unt::kFs));
    for (int off = 0; off < n; off += 256) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, std::min(256, n - off)); }
    return {20 * std::log10(unt::binAmp(l, 3 * f) / unt::binAmp(l, f)), 20 * std::log10(unt::binAmp(r, 3 * f) / unt::binAmp(r, f))};
}
}

TEST_CASE("EQ01 Unit: the shelf and the bell sit a little differently on the two channels (B, C), exactly alike on A") {
    auto setup = [](eq01::Processor& p, int unit) { p.setParam(eq01::Drive, 0); p.setParam(eq01::LowGain, 10); p.setParam(eq01::LowFreq, 100); p.setParam(eq01::AirGain, 10); p.setParam(eq01::AirFreq, 8000); p.setParam(eq01::Unit, unit); };
    CHECK(slopeSpread<eq01::Processor>(0, setup, {50, 100, 160, 5000, 8000, 12000}) == 0.0);
    for (int unit : {1, 2}) { const double d = slopeSpread<eq01::Processor>(unit, setup, {50, 100, 160, 5000, 8000, 12000}); INFO("Unit " << unit << ": " << d << " dB"); CHECK(d > 0.02); CHECK(d < 0.8); }
    auto sat = [](eq01::Processor& p, int unit) { p.setParam(eq01::Drive, 10); p.setParam(eq01::Unit, unit); };
    const auto a = harmonicLR<eq01::Processor>(0, sat, 1000, 0.3), b = harmonicLR<eq01::Processor>(1, sat, 1000, 0.3);
    CHECK(a.first == a.second); CHECK(std::abs(b.first - b.second) > 0.1); CHECK(std::abs(b.first - b.second) < 3.0);
}
TEST_CASE("EQ03 Unit: the dip and the peak, the saturation") {
    auto setup = [](eq03::Processor& p, int unit) { p.setParam(eq03::Drive, 0); p.setParam(eq03::Dip, 10); p.setParam(eq03::DipFreq, 1000); p.setParam(eq03::Peak, 10); p.setParam(eq03::PeakFreq, 3000); p.setParam(eq03::Width, 2.0); p.setParam(eq03::Unit, unit); };
    CHECK(slopeSpread<eq03::Processor>(0, setup, {700, 1000, 1400, 2100, 3000, 4200}) == 0.0);
    for (int unit : {1, 2}) { const double d = slopeSpread<eq03::Processor>(unit, setup, {700, 1000, 1400, 2100, 3000, 4200}); INFO("Unit " << unit << ": " << d << " dB"); CHECK(d > 0.02); CHECK(d < 1.5); }
    auto sat = [](eq03::Processor& p, int unit) { p.setParam(eq03::Drive, 10); p.setParam(eq03::Unit, unit); };
    CHECK(harmonicLR<eq03::Processor>(0, sat, 1000, 0.3).first == harmonicLR<eq03::Processor>(0, sat, 1000, 0.3).second);
    const auto b = harmonicLR<eq03::Processor>(2, sat, 1000, 0.3); CHECK(std::abs(b.first - b.second) > 0.1);
}
TEST_CASE("EQ04 Unit: the filters and the saturation") {
    auto setup = [](eq04::Processor& p, int unit) { p.setParam(eq04::Drive, 0); p.setParam(eq04::Iron, 0); p.setParam(eq04::LowGain, 12); p.setParam(eq04::LowFreq, 110); p.setParam(eq04::MidGain, 12); p.setParam(eq04::MidFreq, 1600); p.setParam(eq04::HighGain, 12); p.setParam(eq04::HighFreq, 12000); p.setParam(eq04::Hpf, 80); p.setParam(eq04::Unit, unit); };
    CHECK(slopeSpread<eq04::Processor>(0, setup, {50, 80, 110, 220, 800, 1600, 3200, 8000, 12000}) == 0.0);
    for (int unit : {1, 2}) { const double d = slopeSpread<eq04::Processor>(unit, setup, {50, 80, 110, 220, 800, 1600, 3200, 8000, 12000}); INFO("Unit " << unit << ": " << d << " dB"); CHECK(d > 0.02); CHECK(d < 1.5); }
    auto sat = [](eq04::Processor& p, int unit) { p.setParam(eq04::Drive, 10); p.setParam(eq04::Unit, unit); };
    const auto b = harmonicLR<eq04::Processor>(1, sat, 1000, 0.3); CHECK(std::abs(b.first - b.second) > 0.1);
}
TEST_CASE("EQ09 Unit: the tilt pivot, the low and the air shelves") {
    auto setup = [](eq09::Processor& p, int unit) { p.setParam(eq09::Tilt, 6); p.setParam(eq09::PivotHz, 1000); p.setParam(eq09::LowLift, 6); p.setParam(eq09::Air, 6); p.setParam(eq09::AutoPivot, 0); p.setParam(eq09::Unit, unit); };
    CHECK(slopeSpread<eq09::Processor>(0, setup, {40, 60, 120, 500, 1000, 2000, 8000, 12000, 16000}) == 0.0);
    for (int unit : {1, 2}) { const double d = slopeSpread<eq09::Processor>(unit, setup, {40, 60, 120, 500, 1000, 2000, 8000, 12000, 16000}); INFO("Unit " << unit << ": " << d << " dB"); CHECK(d > 0.02); CHECK(d < 1.0); }
}
TEST_CASE("EQ05 Unit: the six bands and the filters, the saturation") {
    auto setup = [](eq05::Processor& p, int unit) { p.setParam(eq05::Drive, 0); p.setParam(eq05::HfGain, 10); p.setParam(eq05::HfFreq, 8000); p.setParam(eq05::HmfGain, 10); p.setParam(eq05::HmfFreq, 2000); p.setParam(eq05::LmfGain, 10); p.setParam(eq05::LmfFreq, 600); p.setParam(eq05::LfGain, 10); p.setParam(eq05::LfFreq, 100); p.setParam(eq05::Unit, unit); };
    const std::initializer_list<double> fs = {50, 100, 200, 400, 600, 1000, 2000, 4000, 8000, 12000};
    CHECK(slopeSpread<eq05::Processor>(0, setup, fs) == 0.0);
    for (int unit : {1, 2}) { const double d = slopeSpread<eq05::Processor>(unit, setup, fs); INFO("Unit " << unit << ": " << d << " dB"); CHECK(d > 0.02); CHECK(d < 1.5); }
    auto sat = [](eq05::Processor& p, int unit) { p.setParam(eq05::Drive, 10); p.setParam(eq05::Unit, unit); };
    const auto a = harmonicLR<eq05::Processor>(0, sat, 1000, 0.3), b = harmonicLR<eq05::Processor>(1, sat, 1000, 0.3);
    CHECK(a.first == a.second); CHECK(std::abs(b.first - b.second) > 0.1);
}
TEST_CASE("EQ06 Unit: the three bands, the saturation") {
    auto setup = [](eq06::Processor& p, int unit) { p.setParam(eq06::Drive, 0); p.setParam(eq06::LowGain, 12); p.setParam(eq06::LowHz, 100); p.setParam(eq06::MidGain, 12); p.setParam(eq06::MidHz, 1500); p.setParam(eq06::HighGain, 12); p.setParam(eq06::HighHz, 10000); p.setParam(eq06::Unit, unit); };
    const std::initializer_list<double> fs = {50, 100, 200, 800, 1500, 3000, 6000, 10000, 14000};
    CHECK(slopeSpread<eq06::Processor>(0, setup, fs) == 0.0);
    for (int unit : {1, 2}) { const double d = slopeSpread<eq06::Processor>(unit, setup, fs); INFO("Unit " << unit << ": " << d << " dB"); CHECK(d > 0.02); CHECK(d < 1.5); }
    auto sat = [](eq06::Processor& p, int unit) { p.setParam(eq06::Drive, 10); p.setParam(eq06::Unit, unit); };
    const auto a = harmonicLR<eq06::Processor>(0, sat, 1000, 0.3), b = harmonicLR<eq06::Processor>(2, sat, 1000, 0.3);
    CHECK(a.first == a.second); CHECK(std::abs(b.first - b.second) > 0.1);
}

// ---- compressors and strips: where the colour / tube / iron saturates
TEST_CASE("DY01 and DY06 Unit: where the colour stage / the tube stage saturates differs a little between the channels (B, C), not on A") {
    auto dy01 = [](dy01::Processor& p, int unit) { p.setParam(dy01::Drive, 4); p.setParam(dy01::Color, 1); p.setParam(dy01::Unit, unit); };
    auto dy06 = [](dy06::Processor& p, int unit) { p.setParam(dy06::Input, 10); p.setParam(dy06::Unit, unit); };
    for (int unit : {1, 2}) {
        const auto a = harmonicLR<dy01::Processor>(unit, dy01, 1000, 0.1), b = harmonicLR<dy06::Processor>(unit, dy06, 1000, 0.1);
        INFO("Unit " << unit << ": DY01 " << a.first << " / " << a.second << " dB, DY06 " << b.first << " / " << b.second << " dB");
        CHECK(std::abs(a.first - a.second) > 0.05); CHECK(std::abs(a.first - a.second) < 3.0); CHECK(std::abs(b.first - b.second) > 0.05); CHECK(std::abs(b.first - b.second) < 3.0);
    }
    const auto a = harmonicLR<dy01::Processor>(0, dy01, 1000, 0.1), b = harmonicLR<dy06::Processor>(0, dy06, 1000, 0.1);
    CHECK(a.first == a.second); CHECK(b.first == b.second);
}
TEST_CASE("CS01 and CS03 Unit: the EQ parts and the iron / transformer") {
    auto cs01f = [](cs01::Processor& p, int unit) { p.setParam(cs01::Drive, 0); p.setParam(cs01::Low, 12); p.setParam(cs01::Mid, 12); p.setParam(cs01::MidFreq, 1600); p.setParam(cs01::High, 12); p.setParam(cs01::Hpf, 80); p.setParam(cs01::Unit, unit); };
    auto cs03f = [](cs03::Processor& p, int unit) { p.setParam(cs03::Gain, 30); p.setParam(cs03::Low, 12); p.setParam(cs03::Mid, 12); p.setParam(cs03::High, 12); p.setParam(cs03::Unit, unit); };
    const std::initializer_list<double> fs = {50, 100, 200, 800, 1500, 3000, 6000, 10000, 14000};
    CHECK(slopeSpread<cs01::Processor>(0, cs01f, fs) == 0.0); CHECK(slopeSpread<cs03::Processor>(0, cs03f, fs) == 0.0);
    for (int unit : {1, 2}) {
        const double d1 = slopeSpread<cs01::Processor>(unit, cs01f, fs), d3 = slopeSpread<cs03::Processor>(unit, cs03f, fs);
        INFO("Unit " << unit << ": CS01 " << d1 << " dB, CS03 " << d3 << " dB");
        CHECK(d1 > 0.02); CHECK(d1 < 1.5); CHECK(d3 > 0.02); CHECK(d3 < 1.5);
    }
    auto cs01s = [](cs01::Processor& p, int unit) { p.setParam(cs01::Drive, 10); p.setParam(cs01::Unit, unit); };
    auto cs03s = [](cs03::Processor& p, int unit) { p.setParam(cs03::Gain, 60); p.setParam(cs03::Unit, unit); };
    const auto a0 = harmonicLR<cs01::Processor>(0, cs01s, 100, 0.3), a1 = harmonicLR<cs01::Processor>(1, cs01s, 100, 0.3), c0 = harmonicLR<cs03::Processor>(0, cs03s, 100, 0.05), c1 = harmonicLR<cs03::Processor>(1, cs03s, 100, 0.05);
    CHECK(a0.first == a0.second); CHECK(std::abs(a1.first - a1.second) > 0.05); CHECK(c0.first == c0.second); CHECK(std::abs(c1.first - c1.second) > 0.05);
}
TEST_CASE("GT01 and GT04 Unit: the tube stages, the amp's tone controls") {
    auto gt01 = [](gt01::Processor& p, int unit) { p.setParam(gt01::Gain, 8); p.setParam(gt01::Unit, unit); };
    auto gt04 = [](gt04::Processor& p, int unit) { p.setParam(gt04::Gain, 8); p.setParam(gt04::Drive, 6); p.setParam(gt04::Di, 0); p.setParam(gt04::Unit, unit); };
    const auto a0 = harmonicLR<gt01::Processor>(0, gt01, 500, 0.1), a1 = harmonicLR<gt01::Processor>(2, gt01, 500, 0.1), b0 = harmonicLR<gt04::Processor>(0, gt04, 200, 0.1), b1 = harmonicLR<gt04::Processor>(1, gt04, 200, 0.1);
    INFO("GT01 C " << a1.first << " / " << a1.second << ", GT04 B " << b1.first << " / " << b1.second);
    CHECK(a0.first == a0.second); CHECK(std::abs(a1.first - a1.second) > 0.05); CHECK(b0.first == b0.second); CHECK(std::abs(b1.first - b1.second) > 0.05);
    auto eq = [](gt04::Processor& p, int unit) { p.setParam(gt04::Drive, 0); p.setParam(gt04::Gain, 5); p.setParam(gt04::Di, 0); p.setParam(gt04::Low, 10); p.setParam(gt04::LoMid, 10); p.setParam(gt04::HiMid, 10); p.setParam(gt04::High, 10); p.setParam(gt04::Unit, unit); };
    const std::initializer_list<double> fs = {60, 100, 200, 400, 800, 1600, 3200, 5000};
    CHECK(slopeSpread<gt04::Processor>(0, eq, fs, 0.001) == 0.0);
    CHECK(slopeSpread<gt04::Processor>(1, eq, fs, 0.001) > 0.02);
}
TEST_CASE("SA01 / SA03 / SA04 Unit: the tape, the tube and the iron saturate a little differently on the two channels, and their tone parts sit a little differently") {
    auto sa01s = [](sa01::Processor& p, int unit) { p.setParam(sa01::Wow, 0); p.setParam(sa01::Flutter, 0); p.setParam(sa01::Hiss, -90); p.setParam(sa01::Input, 18); p.setParam(sa01::Saturation, 10); p.setParam(sa01::Unit, unit); };
    auto sa03s = [](sa03::Processor& p, int unit) { p.setParam(sa03::Drive, 10); p.setParam(sa03::Unit, unit); };
    auto sa04s = [](sa04::Processor& p, int unit) { p.setParam(sa04::Gain, 60); p.setParam(sa04::Unit, unit); };
    const double lev = 0.4;
    const auto t0 = harmonicLR<sa01::Processor>(0, sa01s, 1000, lev), t1 = harmonicLR<sa01::Processor>(1, sa01s, 1000, lev), u0 = harmonicLR<sa03::Processor>(0, sa03s, 1000, 0.3), u1 = harmonicLR<sa03::Processor>(2, sa03s, 1000, 0.3), i0 = harmonicLR<sa04::Processor>(0, sa04s, 1000, 0.1), i1 = harmonicLR<sa04::Processor>(1, sa04s, 1000, 0.1);
    INFO("SA01 B " << t1.first << " / " << t1.second << ", SA03 C " << u1.first << " / " << u1.second << ", SA04 B " << i1.first << " / " << i1.second);
    CHECK(t0.first == t0.second); CHECK(std::abs(t1.first - t1.second) > 0.005);   // tape saturates gently (its gain g is small): a part of a per cent of a dB in the 3rd harmonic
    CHECK(u0.first == u0.second); CHECK(std::abs(u1.first - u1.second) > 0.05);
    CHECK(i0.first == i0.second); CHECK(std::abs(i1.first - i1.second) > 0.05);
    auto sa04f = [](sa04::Processor& p, int unit) { p.setParam(sa04::Gain, 20); p.setParam(sa04::LowWeight, 10); p.setParam(sa04::TopAir, 10); p.setParam(sa04::Unit, unit); };
    auto sa03f = [](sa03::Processor& p, int unit) { p.setParam(sa03::Drive, 0); p.setParam(sa03::Tone, 6); p.setParam(sa03::Unit, unit); };
    auto sa01f = [](sa01::Processor& p, int unit) { p.setParam(sa01::Wow, 0); p.setParam(sa01::Flutter, 0); p.setParam(sa01::Hiss, -90); p.setParam(sa01::Input, 0); p.setParam(sa01::Saturation, 0); p.setParam(sa01::Repro, 1); p.setParam(sa01::Unit, unit); };
    CHECK(slopeSpread<sa04::Processor>(0, sa04f, {60, 120, 240, 4000, 8000, 12000}, 0.001) == 0.0);
    CHECK(slopeSpread<sa04::Processor>(1, sa04f, {60, 120, 240, 4000, 8000, 12000}, 0.001) > 0.02);
    CHECK(slopeSpread<sa03::Processor>(0, sa03f, {300, 600, 1000, 1700, 3000}, 0.01) == 0.0);
    CHECK(slopeSpread<sa03::Processor>(2, sa03f, {300, 600, 1000, 1700, 3000}, 0.01) > 0.02);
    CHECK(slopeSpread<sa01::Processor>(0, sa01f, {60, 100, 200, 5000, 9000, 14000}, 0.01) == 0.0);
    CHECK(slopeSpread<sa01::Processor>(1, sa01f, {60, 100, 200, 5000, 9000, 14000}, 0.01) > 0.01);
}
TEST_CASE("SA02 Unit: B and C move its channels' own spread by their fixed part") {
    auto setup = [](sa02::Processor& p, int unit) { p.setSeed(4242); p.setParam(sa02::Drive, 10); p.setParam(sa02::Unit, unit); };
    const auto a = harmonicLR<sa02::Processor>(0, setup, 1000, 0.3), b = harmonicLR<sa02::Processor>(1, setup, 1000, 0.3);
    INFO("A " << a.first << " / " << a.second << ", B " << b.first << " / " << b.second);
    CHECK(std::abs((b.first - b.second) - (a.first - a.second)) > 0.05);
    CHECK(std::abs((b.first - a.first)) < 3.0);
}
