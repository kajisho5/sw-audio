#include "doctest.h"
#include "gt01/gt01.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::gt01;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> tone(double rmsDbfs, double f, double sec) { return sine(rmsDbfs, sec, f); }
double at(const std::vector<float>& y, double f) { return binDb(y, f, y.size() / 2, y.size()); }
// total harmonic distortion of a steady tone (harmonics 2..8 against the fundamental), dB
double thd(const std::vector<float>& y, double f) { double e = 0; for (int k = 2; k <= 8; ++k) e += std::pow(10.0, at(y, f * k) / 10.0); return 10 * std::log10(e + 1e-30) - at(y, f); }
Set amp(int ch, double gain, double master = 5) { return {{Channel, static_cast<double>(ch)}, {Gain, gain}, {Master, master}, {Bass, 5}, {Middle, 5}, {Treble, 5}, {Presence, 5}, {Bright, 0}}; }
Set with(Set a, Set b) { a.insert(a.end(), b.begin(), b.end()); return a; }
}

TEST_CASE("GT01 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"gt01.channel", "gt01.gain", "gt01.bass", "gt01.middle", "gt01.treble", "gt01.presence", "gt01.master", "gt01.bright", "gt01.evo.on"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Channel].labels == std::vector<std::string>{"Clean", "Crunch", "Lead"}); CHECK(s[Channel].def == 1);
    for (int i : {Gain, Bass, Middle, Treble, Presence, Master}) { CHECK(s[static_cast<size_t>(i)].min == 0); CHECK(s[static_cast<size_t>(i)].max == 10); CHECK(s[static_cast<size_t>(i)].def == 5); }
    CHECK(s[Bright].def == 0); CHECK(s[VolumeMatch].def == 0);
}
TEST_CASE("GT01 the tone stack: passive, real stable poles' response, each knob does its job") {
    // passive: never above 0 dB, at any knob position and frequency
    for (double t : {0.0, 0.5, 1.0}) for (double m : {0.0, 0.5, 1.0}) for (double l : {0.0, 0.5, 1.0})
        for (double f = 20; f < 20000; f *= 1.2) CHECK(toneStackAnalogDb(f, t, m, l) <= 0.01);
    // real, negative poles (an RC network has no others): the cubic's discriminant is not negative on a fine grid of knob positions
    for (double t = 0; t <= 1.0001; t += 0.1) for (double m = 0; m <= 1.0001; m += 0.1) for (double l = 0; l <= 1.0001; l += 0.1) {
        const ToneStackPoly p = toneStackAnalog(t, m, l); const double a = p.a[3], b = p.a[2], c = p.a[1];
        CHECK(18 * a * b * c - 4 * b * b * b + b * b * c * c - 4 * a * c * c * c - 27 * a * a > -1e-9 * b * b * c * c);
        CHECK(a > 0); CHECK(b > 0); CHECK(c > 0);
    }
    // each knob raises its own region
    CHECK(toneStackAnalogDb(100, 0.5, 0.5, 1.0) > toneStackAnalogDb(100, 0.5, 0.5, 0.0) + 8.0);
    CHECK(toneStackAnalogDb(5000, 1.0, 0.5, 0.5) > toneStackAnalogDb(5000, 0.0, 0.5, 0.5) + 10.0);
    CHECK(toneStackAnalogDb(640, 0.5, 1.0, 0.5) > toneStackAnalogDb(640, 0.5, 0.0, 0.5) + 5.0);
    // noon has the mid scoop: the lows and highs come through more than the mids
    const double lo = toneStackAnalogDb(80, 0.5, 0.5, bassPot(5)), mid = toneStackAnalogDb(500, 0.5, 0.5, bassPot(5)), hi = toneStackAnalogDb(6000, 0.5, 0.5, bassPot(5));
    CHECK(mid < lo - 4.0); CHECK(mid < hi - 4.0);
    // the bass knob has an audio taper: noon is well below half
    CHECK(bassPot(5) < 0.25); CHECK(bassPot(0) == 0.0); NEAR(bassPot(10), 1.0, 1e-9);
}
TEST_CASE("GT01 the digital tone stack follows the analog one") {
    auto p = make({{Bass, 8}, {Middle, 3}, {Treble, 7}});
    for (double f : {80.0, 200.0, 500.0, 1000.0, 3000.0})
        NEAR(p.toneResponseDb(f), toneStackAnalogDb(f, 0.7, 0.3, bassPot(8)), 0.5);
}
TEST_CASE("GT01 no delay; silence is silence; no DC comes out") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(amp(Lead, 10, 10)); std::vector<float> z(24000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
    auto r = make(amp(Lead, 10, 10)); std::vector<float> dc(96000, 0.1f); const auto y = run(r, dc);
    CHECK(peakDb(y, 72000, 96000) < -60.0);
}
TEST_CASE("GT01 small signals are linear; the level follows the input") {
    auto a = make(amp(Clean, 3)); auto b = make(amp(Clean, 3));
    const auto ya = run(a, tone(-60, 1000, 1.0)), yb = run(b, tone(-54, 1000, 1.0));
    NEAR(at(yb, 1000) - at(ya, 1000), 6.02, 0.3);
    CHECK(thd(ya, 1000) < -50.0);
}
TEST_CASE("GT01 Gain raises the distortion; the channels are ordered Clean < Crunch < Lead") {
    auto lo = make(amp(Crunch, 2)); auto hi = make(amp(Crunch, 9));
    const auto x = tone(-24, 1000, 1.0);
    CHECK(thd(run(hi, x), 1000) > thd(run(lo, x), 1000) + 10.0);
    double prev = -200;
    for (int ch : {Clean, Crunch, Lead}) { auto p = make(amp(ch, 5)); const double d = thd(run(p, x), 1000); CHECK(d > prev + 4.0); prev = d; }
}
TEST_CASE("GT01 even harmonics from the triodes") {
    auto p = make(amp(Crunch, 6)); const auto y = run(p, tone(-24, 1000, 1.0));
    CHECK(at(y, 2000) > at(y, 1000) - 40.0);
}
TEST_CASE("GT01 turning the guitar down cleans the sound up") {
    auto a = make(amp(Crunch, 6)); auto b = make(amp(Crunch, 6));
    const double d0 = thd(run(a, tone(-24, 1000, 1.0)), 1000), d1 = thd(run(b, tone(-36, 1000, 1.0)), 1000);
    CHECK(d1 < d0 - 10.0);
}
TEST_CASE("GT01 Master sets the output; 0 is nearly mute") {
    const auto x = tone(-24, 1000, 1.0);
    auto m5 = make(amp(Crunch, 5, 5)); auto m8 = make(amp(Crunch, 5, 8)); auto m0 = make(amp(Crunch, 5, 0));
    const double l5 = rmsDb(run(m5, x)), l8 = rmsDb(run(m8, x)), l0 = rmsDb(run(m0, x));
    CHECK(l8 > l5 + 3.0); CHECK(l0 < l5 - 40.0);
}
TEST_CASE("GT01 the power supply sags: a loud onset is higher than the sustained level") {
    std::vector<float> x(static_cast<size_t>(1.5 * kFs), 0.0f); const auto t = tone(-14, 200, 1.0); x.insert(x.begin() + 24000, t.begin(), t.end()); x.resize(static_cast<size_t>(1.5 * kFs));
    auto p = make(amp(Crunch, 5, 9)); const auto y = run(p, x);
    const double head = rmsDb(y, 24000, 24000 + 2400), later = rmsDb(y, 24000 + 36000, 24000 + 46000);
    CHECK(head > later + 1.0);
}
TEST_CASE("GT01 Presence opens the top; Bright helps at low Gain only") {
    const auto x = tone(-30, 6000, 1.0);
    auto p0 = make(with(amp(Clean, 3), {{Presence, 0}})); auto p10 = make(with(amp(Clean, 3), {{Presence, 10}}));
    NEAR(at(run(p10, x), 6000) - at(run(p0, x), 6000), 12.0, 3.0);
    const auto xt = tone(-40, 4000, 1.0), xb = tone(-40, 400, 1.0);
    auto off = make(amp(Clean, 2)); auto on = make(with(amp(Clean, 2), {{Bright, 1}}));
    const double dLow = (at(run(on, xt), 4000) - at(xt, 4000)) - (at(run(off, xt), 4000) - at(xt, 4000));
    CHECK(dLow > 4.0);
    auto offH = make(amp(Clean, 10)); auto onH = make(with(amp(Clean, 10), {{Bright, 1}}));
    const double dHigh = at(run(onH, xt), 4000) - at(run(offH, xt), 4000);
    CHECK(dHigh < 1.5); (void)xb;
}
TEST_CASE("GT01 4x oversampling keeps the aliases down") {
    auto p = make(amp(Lead, 10, 8)); const auto y = run(p, tone(-12, 5000, 1.0));
    CHECK(at(y, 18000) < at(y, 5000) - 50.0);   // 6th harmonic (30 kHz) folds to 18 kHz
}
TEST_CASE("GT01 Volume match: measures the playing level once and fixes the gain") {
    auto p = make(with(amp(Crunch, 5), {{VolumeMatch, 1}}));
    CHECK(!p.matchDone());
    const auto x = tone(-34, 1000, 8.0); run(p, x);
    CHECK(p.matchDone()); NEAR(p.matchGainDb(), 14.0, 1.0);   // -34 dBFS to the -20 dBFS reference
    auto q = make(amp(Crunch, 5)); run(q, x); CHECK(!q.matchDone()); NEAR(q.matchGainDb(), 0.0, 1e-9);
    auto r = make(with(amp(Crunch, 5), {{VolumeMatch, 1}})); run(r, tone(-70, 1000, 8.0)); CHECK(!r.matchDone());   // too quiet to count as playing
}
TEST_CASE("GT01 the channels are independent") {
    auto p = make(amp(Lead, 8)); const auto l = tone(-20, 500, 1.0); std::vector<float> sil(l.size(), 0.0f);
    const auto o = run2(p, l, sil); for (float v : o.second) CHECK(v == 0.0f);
}
