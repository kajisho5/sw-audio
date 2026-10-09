#include "doctest.h"
#include "ms06/ms06.hpp"
#include "os_helpers.hpp"
#include "sw/loudness.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::ms06;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; p.setParam(GainMatch, 0); for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
double toneDb(Set s, double f, double db = -30, double sec = 2) { auto p = make(s); const auto y = run(p, sine(db, sec, f)); return rmsDb(y) - db; }
}

TEST_CASE("MS06 table: stage parameters follow the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(std::string(s[EqTilt].id) == "ms06.eq.tilt"); CHECK(s[EqTilt].min == -6); CHECK(s[EqTilt].max == 6);
    CHECK(s[EqLow].min == -6); CHECK(s[EqHigh].max == 6); CHECK(s[EqBell].min == -6); CHECK(s[EqBellFreq].min == 200); CHECK(s[EqBellFreq].max == 8000);
    CHECK(s[CompThresh].min == -40); CHECK(s[CompThresh].max == 0); CHECK(s[CompThresh].def == 0);
    CHECK(s[CompRatio].min == 1); CHECK(s[CompRatio].max == 4); CHECK(s[CompRatio].def == 1.5);
    CHECK(s[CompAttack].min == 1); CHECK(s[CompAttack].max == 100); CHECK(s[CompAttack].def == 30);
    CHECK(s[CompRelease].min == 20); CHECK(s[CompRelease].max == 1000); CHECK(std::string(s[CompRelease].maxLabel) == "Auto"); CHECK(s[CompRelease].def == 1000);
    CHECK(s[CompMix].def == 100);
    CHECK(s[SatDrive].min == 0); CHECK(s[SatDrive].max == 12); CHECK(s[SatMix].def == 100);
    CHECK(s[Width].min == 0); CHECK(s[Width].max == 200); CHECK(s[Width].def == 100);
    CHECK(s[MonoBelow].min == 20); CHECK(s[MonoBelow].max == 300); CHECK(std::string(s[MonoBelow].minLabel) == "Off");
    CHECK(s[LimitGain].min == 0); CHECK(s[LimitGain].max == 24); CHECK(s[LimitCeiling].min == -12); CHECK(s[LimitCeiling].max == 0);
    CHECK(s[GainMatch].def == 1); CHECK(s[Order].max == 119); CHECK_FALSE(s[Order].automatable); CHECK(s[Order].def == 0);
    CHECK_FALSE(s[RefAB].automatable);
    for (int on : {EqOn, CompOn, SatOn, WidthOn, LimitOn}) CHECK(s[static_cast<size_t>(on)].labels == std::vector<std::string>{"Off", "On"});
}
TEST_CASE("MS06 the order is one choice of 120 (Lehmer code) and round-trips") {
    for (int i = 0; i < 120; ++i) CHECK(indexFromOrder(orderFromIndex(i)) == i);
    CHECK(orderFromIndex(0) == std::array<int, kStages>{0, 1, 2, 3, 4});
}
TEST_CASE("MS06 default settings do not change the sound; latency is the limiter's only while Limit is on") {
    for (double f : {60.0, 500.0, 5000.0, 15000.0}) NEAR(toneDb({{LimitOn, 0}}, f), 0.0, 0.3);
    Processor p; CHECK(p.latencySamples() == 72 + 16);
    p.setParam(LimitOn, 0); CHECK(p.latencySamples() == 0);
}
TEST_CASE("MS06 EQ: Tilt around 1 kHz, shelves at 80 Hz and 12 kHz, Bell with Q 0.7") {
    const Set off = {{LimitOn, 0}};
    auto with = [&](Set s) { Set t = off; t.insert(t.end(), s.begin(), s.end()); return t; };
    NEAR(toneDb(with({{EqTilt, 6}}), 100), -5.0, 1.5); NEAR(toneDb(with({{EqTilt, 6}}), 1000), 0.0, 0.5); NEAR(toneDb(with({{EqTilt, 6}}), 10000), 5.0, 1.5);
    NEAR(toneDb(with({{EqLow, 6}}), 30), 6.0, 0.8); NEAR(toneDb(with({{EqLow, 6}}), 2000), 0.0, 0.3);
    NEAR(toneDb(with({{EqHigh, -6}}), 18000), -6.0, 1.0); NEAR(toneDb(with({{EqHigh, -6}}), 500), 0.0, 0.3);
    NEAR(toneDb(with({{EqBell, 6}, {EqBellFreq, 3000}}), 3000), 6.0, 0.5); NEAR(toneDb(with({{EqBell, 6}, {EqBellFreq, 3000}}), 100), 0.0, 0.3);
}
TEST_CASE("MS06 Comp: 4:1 above the threshold; Mix blends the dry back") {
    const Set s = {{LimitOn, 0}, {CompThresh, -20}, {CompRatio, 4}, {CompAttack, 1}, {CompRelease, 50}};
    NEAR(toneDb(s, 1000, -8) , -9.0, 1.5);   // 12 dB over -> 9 dB of reduction
    Set m = s; m.push_back({CompMix, 0}); NEAR(toneDb(m, 1000, -8), 0.0, 0.2);
}
TEST_CASE("MS06 Saturate adds harmonics with Drive; Mix 0 is clean") {
    auto h3 = [](double drive, double mix) { auto p = make({{LimitOn, 0}, {SatOn, 1}, {SatDrive, drive}, {SatMix, mix}}); return harmDb(run(p, sine(-6, 2, 1000)), 1000, 3); };
    CHECK(h3(12, 100) > h3(0, 100) + 10.0); CHECK(h3(12, 0) < -80.0);
}
TEST_CASE("MS06 Width: 0 % is mono, 200 % doubles the sides; Mono below keeps the lows centred") {
    auto side = [](Set s, double f) { auto p = make(s); const auto n = static_cast<size_t>(2 * kFs); std::vector<float> l(n), r(n); const auto t = sine(-20, 2, f); for (size_t i = 0; i < n; ++i) { l[i] = t[i]; r[i] = -t[i]; } const auto [yl, yr] = run2(p, l, r); return rmsDb(yl) - -20.0; };
    CHECK(side({{LimitOn, 0}, {Width, 0}}, 1000) < -50.0);
    NEAR(side({{LimitOn, 0}, {Width, 200}}, 1000), 6.0, 0.5);
    CHECK(side({{LimitOn, 0}, {MonoBelow, 300}}, 100) < -12.0);     // 100 Hz out of phase: pulled to the centre
    NEAR(side({{LimitOn, 0}, {MonoBelow, 300}}, 3000), 0.0, 0.5);   // the highs keep their width
}
TEST_CASE("MS06 Limit: gain up to the ceiling") {
    auto p = make({{LimitGain, 12}, {LimitCeiling, -6}});
    const auto y = run(p, noise(-14, 3, 2));
    CHECK(peakDb(y, 48000, y.size()) <= -6.0 + 0.1);
}
TEST_CASE("MS06 the order matters (Comp before / after Saturate) and a stage that is Off leaves it alone") {
    auto out = [](int order) { auto p = make({{LimitOn, 0}, {CompOn, 1}, {SatOn, 1}, {CompThresh, -30}, {CompRatio, 4}, {SatDrive, 12}, {Order, static_cast<double>(order)}}); return run(p, sine(-10, 2, 1000)); };
    // order 0 = EQ Comp Sat Width Limit; find an order with Sat before Comp: EQ Sat Comp Width Limit
    std::array<int, kStages> a = {0, 2, 1, 3, 4}, b = {0, 1, 2, 3, 4};
    const auto o1 = out(indexFromOrder(a)), o2 = out(indexFromOrder(b));
    double d = 0; for (size_t i = 40000; i < o1.size(); ++i) d = std::max(d, static_cast<double>(std::abs(o1[i] - o2[i])));
    CHECK(d > 0.005);
    auto off = [](int order) { auto p = make({{LimitOn, 0}, {CompOn, 0}, {SatOn, 0}, {Order, static_cast<double>(order)}}); return run(p, sine(-10, 1, 1000)); };
    const auto q1 = off(0), q2 = off(57);
    for (size_t i = 24000; i < q1.size(); ++i) REQUIRE(std::abs(q1[i] - q2[i]) < 1e-5);
}
TEST_CASE("MS06 Gain match keeps the loudness (K-weighted) of the chain where it was, whatever the stages do") {
    auto lufs = [](const std::vector<float>& x) { IntegratedLoudness m; m.setup(48000.0, 2, 0.0); for (size_t off = x.size() - 384000; off + 480 <= x.size(); off += 480) { const float* c[2] = {x.data() + off, x.data() + off}; m.process(c, 2, 480); } return m.integrated(); };
    auto out = [](int match) { Processor p; p.setParam(GainMatch, match); p.setParam(EqLow, 6); p.setParam(EqTilt, -6); p.setParam(SatOn, 1); p.setParam(SatDrive, 9); p.setParam(LimitGain, 9); p.prepare(kFs, 256); p.snapToTargets(); return run(p, noise(-24, 25, 7)); };
    const double ref = lufs(noise(-24, 25, 7));
    CHECK(std::abs(lufs(out(0)) - ref) > 2.0);
    NEAR(lufs(out(1)), ref, 0.7);
}
TEST_CASE("MS06 silence stays silent, extreme input finite") {
    { auto z = make(); for (float v : run(z, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f); }
    Processor p; p.setParam(SatOn, 1); p.setParam(SatDrive, 12); p.setParam(Width, 200); p.setParam(LimitGain, 24); p.prepare(kFs, 256);
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : run(p, x)) REQUIRE(std::isfinite(v));
}

TEST_CASE("MS06 output meters (the screen's LUFS / TP / LRA): short-term loudness, true peak, loudness range") {
    // the chain at its defaults is transparent: the output meter agrees with a reference meter on the input
    const auto x = sine(-20, 6.0, 1000.0);
    { auto p = make(); run(p, x); LoudnessMeter ref; ref.setup(kFs, 2); std::vector<float> l = x, r = x; for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); const float* c[2] = {l.data() + off, r.data() + off}; ref.process(c, 2, n); }
      NEAR(p.outShortTermLufs(), ref.shortTerm(), 0.3); CHECK(p.outShortTermLufs() > -40.0); }
    // true peak: a sine of -6.02 dBFS RMS peaks at -3.01 dBFS
    { auto p = make(); run(p, sine(-6.02, 2.0, 1000.0)); NEAR(p.outTruePeakDb(), -3.01, 0.15); p.resetMeters(); CHECK(p.outTruePeakDb() < -150.0); CHECK(p.outShortTermLufs() < -150.0); }
    // silence: nothing measured
    { auto p = make(); run(p, std::vector<float>(48000, 0.0f)); CHECK(p.outShortTermLufs() < -150.0); CHECK(p.outTruePeakDb() < -150.0); CHECK(p.outRangeLu() == 0.0); }
    // a steady tone has no range; two levels 10 LU apart give about 10 LU
    { auto p = make(); run(p, sine(-25, 20.0, 1000.0)); CHECK(p.outRangeLu() < 0.5); }
    { auto p = make(); auto a = sine(-30, 20.0, 1000.0), b = sine(-20, 20.0, 1000.0); a.insert(a.end(), b.begin(), b.end()); run(p, a); NEAR(p.outRangeLu(), 10.0, 1.5); }
}

// the common oversampling setting (spec 共通機能: 1x / 2x / 4x, default 2x)
TEST_CASE("MS06: the oversampling parameter is the last one, 1x / 2x / 4x, default 2x, and the Saturate stage follows it") {
    const auto& s = specs();
    CHECK(std::string(s.back().id) == "ms06.os"); CHECK(s[Oversample].steps == std::vector<double>{1, 2, 4}); CHECK(s[Oversample].def == 2.0); CHECK(Oversample == kNumParams - 1);
    auto alias = [](int os) { auto p = make({{SatOn, 1}, {SatDrive, 12}, {Oversample, static_cast<double>(os)}}); return ost::relDb(p, 15000, 3000, 0.3); };
    const double a1 = alias(1), a2 = alias(2), a4 = alias(4);
    INFO("15 kHz through Saturate at Drive 12, alias at 3 kHz: 1x " << a1 << " dB, 2x " << a2 << " dB, 4x " << a4 << " dB");
    CHECK(a1 > -45.0); CHECK(a2 < -60.0); CHECK(ost::notWorse(a4, a2));
}
