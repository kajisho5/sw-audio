#include "doctest.h"
#include "cr01/cr01.hpp"
#include "tu.hpp"
#include "os_helpers.hpp"
using namespace sw;
using namespace sw::cr01;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
double gainDb(Set set, double hz, double inDb = -24.0) { set.push_back({EnvAmount, 0}); auto p = make(set); const auto y = run(p, sine(inDb, 1.5, hz)); return rmsDb(y, 36000, 72000) - inDb; }
}

TEST_CASE("CR01 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"cr01.type", "cr01.mod", "cr01.cutoff", "cr01.res", "cr01.env", "cr01.drive", "cr01.evo.on", "cr01.os"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Type].labels == std::vector<std::string>{"LP", "BP", "HP", "Notch"}); CHECK(s[Type].def == 0);
    CHECK(s[ModSource].labels == std::vector<std::string>{"Envelope", "LFO", "Sidechain"}); CHECK(s[ModSource].def == 0);
    CHECK(s[Cutoff].min == 20); CHECK(s[Cutoff].max == 20000); CHECK(s[Cutoff].def == 1200); CHECK(s[Cutoff].curve == Curve::Log);
    CHECK(s[Resonance].def == 60); CHECK(s[EnvAmount].min == -100); CHECK(s[EnvAmount].max == 100); CHECK(s[EnvAmount].def == 40); CHECK(s[Drive].def == 20);
    CHECK(s[Evo].def == 1);
}
TEST_CASE("CR01 no delay; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("CR01 the four types") {
    const Set base = {{Resonance, 0}, {Drive, 0}, {Cutoff, 1200}};
    auto with = [&](int type, double hz) { Set s = base; s.push_back({Type, double(type)}); return gainDb(s, hz); };
    NEAR(with(LP, 200), 0.0, 1.0); CHECK(with(LP, 6000) < -20.0);
    NEAR(with(HP, 8000), 0.0, 1.0); CHECK(with(HP, 150) < -14.0);
    CHECK(with(BP, 1200) > with(BP, 100) + 10.0); CHECK(with(BP, 1200) > with(BP, 8000) + 10.0);
    CHECK(with(Notch, 1200) < -12.0); NEAR(with(Notch, 100), 0.0, 1.5); NEAR(with(Notch, 9000), 0.0, 1.5);
}
TEST_CASE("CR01 Resonance raises the peak at the cutoff") {
    auto at = [&](double res) { return gainDb({{Resonance, res}, {Drive, 0}, {Cutoff, 1200}}, 1200, -40.0); };
    CHECK(at(100) > at(0) + 15.0); CHECK(at(60) > at(0) + 6.0);
}
TEST_CASE("CR01 Envelope: a louder input opens the filter (Adaptive off: a fixed level range)") {
    auto through = [&](double inDb) { auto p = make({{Evo, 0}, {EnvAmount, 100}, {Resonance, 0}, {Drive, 0}, {Cutoff, 400}}); const auto y = run(p, sine(inDb, 2.0, 3000)); return rmsDb(y, 48000, 96000) - inDb; };
    CHECK(through(-6) > through(-36) + 10.0);
    auto neg = [&](double inDb) { auto p = make({{Evo, 0}, {EnvAmount, -100}, {Resonance, 0}, {Drive, 0}, {Cutoff, 8000}}); const auto y = run(p, sine(inDb, 2.0, 3000)); return rmsDb(y, 48000, 96000) - inDb; };
    CHECK(neg(-6) < neg(-36) - 10.0);   // a negative amount closes it for loud input
}
TEST_CASE("CR01 Adaptive range: the same swing in a quiet passage opens the filter as far") {
    // the signal alternates every 0.5 s between a level L and L - 12 dB; the loud half is measured against the quiet half
    auto swing = [&](double top, double evo) {
        auto p = make({{Evo, evo}, {EnvAmount, 100}, {Resonance, 0}, {Drive, 0}, {Cutoff, 500}}); std::vector<float> x(static_cast<size_t>(24 * kFs), 0.0f);
        const auto t = sine(0, 24.0, 3000); for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(t[i] * std::pow(10.0, ((i / 24000) % 2 == 0 ? top : top - 12.0) / 20.0));
        const auto y = run(p, x); const size_t a = 20 * 48000; double lo = -1e9, hi = -1e9; for (int k = 0; k < 4; ++k) { const size_t s = a + static_cast<size_t>(k) * 24000 + 12000; const double l = rmsDb(y, s, s + 6000) ; if (k % 2 == 0) hi = std::max(hi, l - top); else lo = std::max(lo, l - (top - 12.0)); }
        return hi - lo; };
    const double loudOn = swing(-6, 1), quietOn = swing(-36, 1), quietOff = swing(-36, 0);
    NEAR(quietOn, loudOn, 4.0);       // the same swing, whatever the level
    CHECK(quietOn > quietOff + 3.0);  // Off: the quiet passage barely moves the filter
}
TEST_CASE("CR01 LFO: one cycle per bar at the host tempo; the filter moves") {
    auto p = make({{ModSource, Lfo}, {EnvAmount, 100}, {Resonance, 0}, {Drive, 0}, {Cutoff, 500}}); p.setTempo(240.0);   // 1 bar = 1 s
    const auto y = run(p, sine(-18, 8.0, 3000));
    double mn = 1e9, mx = -1e9; for (size_t s = 48000 * 3; s + 4800 < y.size(); s += 2400) { const double l = rmsDb(y, s, s + 2400); mn = std::min(mn, l); mx = std::max(mx, l); }
    CHECK(mx - mn > 15.0);
    auto q = make({{ModSource, Lfo}, {EnvAmount, 0}, {Resonance, 0}, {Drive, 0}, {Cutoff, 500}}); const auto z = run(q, sine(-18, 4.0, 3000));
    double mn2 = 1e9, mx2 = -1e9; for (size_t s = 48000 * 2; s + 4800 < z.size(); s += 2400) { const double l = rmsDb(z, s, s + 2400); mn2 = std::min(mn2, l); mx2 = std::max(mx2, l); }
    CHECK(mx2 - mn2 < 0.5);
}
TEST_CASE("CR01 Sidechain drives the cutoff; without one the input does") {
    auto pass = [&](double scDb) {
        auto p = make({{ModSource, Sidechain}, {Evo, 0}, {EnvAmount, 100}, {Resonance, 0}, {Drive, 0}, {Cutoff, 400}});
        std::vector<float> l = sine(-24, 2.0, 3000), r = l; const auto s = sine(scDb, 2.0, 100);
        for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; const float* sc[2] = {s.data() + off, s.data() + off}; p.processWithSidechain(c, 2, n, sc, 2); }
        return rmsDb(l, 48000, 96000) + 24.0; };
    CHECK(pass(-3) > pass(-40) + 10.0);
    auto p = make({{ModSource, Sidechain}, {Evo, 0}, {EnvAmount, 100}, {Resonance, 0}, {Drive, 0}, {Cutoff, 400}}); const auto y = run(p, sine(-6, 2.0, 3000));   // no sidechain: the input itself
    CHECK(rmsDb(y, 48000, 96000) > -6.0 - 8.0);
}
TEST_CASE("CR01 Drive adds harmonics") {
    auto h3 = [&](double drive) { auto p = make({{Cutoff, 20000}, {EnvAmount, 0}, {Resonance, 0}, {Drive, drive}}); const auto y = run(p, sine(-6, 2.0, 400)); return harmDb(y, 400, 3); };
    CHECK(h3(100) > -40.0); CHECK(h3(0) < -70.0);
}
TEST_CASE("CR01 loud input stays finite") {
    auto p = make({{Resonance, 100}, {Drive, 100}, {EnvAmount, 100}}); auto x = noise(6, 2.0, 9); for (auto& v : x) v *= 8.0f; for (float v : run(p, x)) CHECK(std::isfinite(v));
}

// the screen draws this response (ui/displays.js cr01Gain, tests/ui/shapes.test.js holds the same table): gain in dB at 100 / 600 / 1200 / 2500 / 5000 / 12000 Hz, Drive 0, 48 kHz.
// A -60 in the table means "a null" (the Notch at its cutoff, the screen's floor): the core must be under -40 there.
TEST_CASE("CR01 the response the screen draws (Type, Resonance, Cutoff) is the one the core has") {
    const double fr[6] = {100.0, 600.0, 1200.0, 2500.0, 5000.0, 12000.0};
    struct Row { int type; double res; double fc; double g[6]; };
    static const Row rows[] = {
        {0, 0.0, 1200, {-0.06, -1.94, -6.02, -14.58, -25.42, -41.00}},
        {0, 0.0, 5000, {-0.00, -0.12, -0.48, -1.91, -6.02, -17.26}},
        {0, 0.6, 1200, {0.04, 1.26, 1.31, -11.61, -24.63, -40.87}},
        {0, 0.6, 5000, {0.00, 0.08, 0.31, 1.25, 1.31, -15.14}},
        {0, 1.0, 1200, {0.06, 2.48, 20.00, -10.53, -24.44, -40.84}},
        {0, 1.0, 5000, {0.00, 0.12, 0.50, 2.44, 20.00, -14.49}},
        {1, 0.0, 1200, {-15.63, -1.94, 0.00, -2.16, -6.93, -14.52}},
        {1, 0.0, 5000, {-28.04, -12.59, -6.93, -1.97, 0.00, -3.25}},
        {1, 0.6, 1200, {-22.86, -6.07, 0.00, -6.53, -13.47, -21.72}},
        {1, 0.6, 5000, {-35.37, -19.73, -13.47, -6.14, 0.00, -8.46}},
        {1, 1.0, 1200, {-41.53, -23.55, 0.00, -24.14, -31.97, -40.38}},
        {1, 1.0, 5000, {-54.05, -38.37, -31.97, -23.64, 0.00, -26.50}},
        {2, 0.0, 1200, {-43.24, -13.98, -6.02, -1.80, -0.48, -0.08}},
        {2, 0.0, 5000, {-60.00, -37.11, -25.42, -14.07, -6.02, -1.28}},
        {2, 0.6, 1200, {-43.14, -10.78, 1.31, 1.17, 0.31, 0.05}},
        {2, 0.6, 5000, {-60.00, -36.91, -24.63, -10.91, 1.31, 0.84}},
        {2, 1.0, 1200, {-43.12, -9.57, 20.00, 2.25, 0.50, 0.08}},
        {2, 1.0, 5000, {-60.00, -36.86, -24.44, -9.72, 20.00, 1.49}},
        {3, 0.0, 1200, {-0.12, -4.43, -60.00, -4.06, -0.98, -0.16}},
        {3, 0.0, 5000, {-0.01, -0.25, -0.98, -4.37, -60.00, -2.78}},
        {3, 0.6, 1200, {-0.02, -1.23, -60.00, -1.09, -0.20, -0.03}},
        {3, 0.6, 5000, {-0.00, -0.05, -0.20, -1.21, -60.00, -0.67}},
        {3, 1.0, 1200, {-0.00, -0.02, -60.00, -0.02, -0.00, -0.00}},
        {3, 1.0, 5000, {-0.00, -0.00, -0.00, -0.02, -60.00, -0.01}},
    };
    for (const auto& r : rows)
        for (int i = 0; i < 6; ++i) {
            const double got = gainDb({{Type, static_cast<double>(r.type)}, {Cutoff, r.fc}, {Resonance, r.res * 100.0}, {Drive, 0.0}}, fr[i]);
            if (r.g[i] <= -59.9) CHECK(got < -40.0); else NEAR(got, r.g[i], 0.3);
        }
}

// the common oversampling setting (spec 共通機能: 1x / 2x / 4x, default 2x)
TEST_CASE("CR01: the oversampling parameter is the last one, 1x / 2x / 4x, default 2x") {
    const auto& s = specs();
    CHECK(s[Oversample].steps == std::vector<double>{1, 2, 4}); CHECK(s[Oversample].def == 2.0); CHECK(Oversample == kNumParams - 1);
}
TEST_CASE("CR01: the filter is the same filter at every oversampling setting (its coefficients follow the rate)") {
    for (double hz : {600.0, 1200.0, 2400.0}) {
        const double g2 = gainDb({{Resonance, 0}, {Drive, 0}, {Cutoff, 1200}, {Oversample, 2}}, hz);
        for (int os : {1, 4}) {
            const double g = gainDb({{Resonance, 0}, {Drive, 0}, {Cutoff, 1200}, {Oversample, double(os)}}, hz);
            INFO(hz << " Hz at " << os << "x: " << g << " dB, at 2x: " << g2 << " dB");
            CHECK(std::abs(g - g2) < 0.3);
        }
    }
}
TEST_CASE("CR01: Drive's saturation folds back less with more oversampling") {
    auto alias = [](int os) { auto p = make({{Type, 0}, {Cutoff, 20000}, {Resonance, 0}, {EnvAmount, 0}, {Drive, 100}, {Oversample, double(os)}}); return ost::relDb(p, 15000, 3000, 0.4); };
    const double a1 = alias(1), a2 = alias(2), a4 = alias(4);
    INFO("15 kHz at Drive 100, alias at 3 kHz: 1x " << a1 << " dB, 2x " << a2 << " dB, 4x " << a4 << " dB");
    CHECK(a1 > -50.0); CHECK(a2 < a1 - 15.0); CHECK(ost::notWorse(a4, a2));
}
