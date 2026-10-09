#include "doctest.h"
#include "eq_helpers.hpp"
#include "cs04/cs04.hpp"
#include <cmath>
#include <random>
#include <vector>
using namespace sw;
using namespace sw::cs04;
namespace {
Processor make(std::vector<std::pair<int, double>> set) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(eqt::kFs, 256); p.snapToTargets(); return p; }
std::vector<float> run(Processor& p, std::vector<float> l) { std::vector<float> r = l; for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } return l; }
std::vector<float> sine(double rmsDb, double f, int n) { const double a = std::pow(10.0, (rmsDb + 3.0103) / 20); std::vector<float> x(static_cast<size_t>(n)); for (int i = 0; i < n; ++i) x[static_cast<size_t>(i)] = static_cast<float>(a * std::sin(2 * eqt::kPi * f * i / eqt::kFs)); return x; }
double rmsDb(const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += x[i] * x[i]; return 10 * std::log10(s / (b - a) + 1e-30); }
}

TEST_CASE("CS04 table: six modules with On, the order as one non-automatable choice of 720") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(std::string(s[EqOn].id) == "cs04.eq.on"); CHECK(s[EqOn].def == 1);
    for (int id : {GateOn, CompOn, SatOn, DeessOn, LimitOn}) { CHECK(s[id].def == 0); CHECK(s[id].automatable); }
    CHECK(std::string(s[EqMidFreq].id) == "cs04.eq.midfreq"); CHECK(s[EqMidFreq].min == 200); CHECK(s[EqMidFreq].max == 8000); CHECK(s[EqMidFreq].def == 2500);
    CHECK(s[EqLow].max == 12); CHECK(s[EqOut].min == -12);
    CHECK(s[GateThresh].min == -80); CHECK(s[GateRange].min == -80); CHECK(s[GateRelease].max == 2000);
    CHECK(s[CompThresh].min == -60); CHECK(s[CompRatio].max == 20); CHECK(s[CompAttack].min == doctest::Approx(0.1)); CHECK(s[CompMakeup].max == 24);
    CHECK(s[SatDrive].max == 24); CHECK(s[SatMix].max == 100);
    CHECK(s[DeessFreq].min == 2000); CHECK(s[DeessFreq].max == 12000); CHECK(s[DeessRange].min == -20);
    CHECK(s[LimitCeiling].min == -12); CHECK(s[LimitRelease].max == 1000);
    CHECK(std::string(s[Order].id) == "cs04.order"); CHECK(s[Order].numSteps() == 720); CHECK_FALSE(s[Order].automatable);
    CHECK(s[Order].labels[0] == "Gate > EQ > Comp > Saturate > De-ess > Limit");
}
TEST_CASE("CS04 order codes: every permutation once, round trip") {
    std::vector<bool> seen(720, false);
    for (int i = 0; i < 720; ++i) { const auto o = orderFromIndex(i); CHECK(indexFromOrder(o) == i); }
    const std::array<int, 6> rev = {5, 4, 3, 2, 1, 0}; CHECK(orderFromIndex(indexFromOrder(rev)) == rev);
}
TEST_CASE("CS04 defaults are transparent and add no latency") {
    auto p = make({});
    CHECK(p.latencySamples() == 0);
    std::mt19937 rng(2); std::normal_distribution<double> nd(0, 0.2); std::vector<float> x(8192); for (auto& v : x) v = static_cast<float>(nd(rng));
    const auto y = run(p, x);
    for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == doctest::Approx(x[i]).epsilon(1e-6));
}
TEST_CASE("CS04 Limit adds 1 ms of look-ahead and holds the ceiling") {
    Processor p; p.setParam(LimitOn, 1); CHECK(p.latencySamples() == 48);
    auto q = make({{LimitOn, 1}, {LimitCeiling, -6}});
    double pk = 0; for (float v : run(q, sine(0, 1000, 24000))) pk = std::max(pk, (double)std::abs(v));
    CHECK(20 * std::log10(pk) <= -6.0 + 1e-4);
}
TEST_CASE("CS04 EQ module") {
    { auto p = make({{EqMid, 12}}); CHECK(eqt::gainDb(p, 2500) == doctest::Approx(12).epsilon(0.01)); }
    { auto p = make({{EqLow, -12}}); CHECK(eqt::gainDb(p, 25) < -11.0); }
    { auto p = make({{EqOut, 6}}); CHECK(eqt::gainDb(p, 1000) == doctest::Approx(6).epsilon(0.01)); }
    { auto p = make({{EqMid, 12}, {EqOn, 0}}); CHECK(std::abs(eqt::gainDb(p, 2500)) < 0.01); }
}
TEST_CASE("CS04 Comp module: -20 dB threshold, 4:1") {
    auto p = make({{CompOn, 1}, {CompThresh, -20}, {CompRatio, 4}, {CompAttack, 1}});
    CHECK(rmsDb(run(p, sine(-10, 1000, 96000)), 48000, 96000) == doctest::Approx(-17.5).epsilon(0.03));
}
TEST_CASE("CS04 Gate module closes the quiet tail") {
    auto p = make({{GateOn, 1}, {GateThresh, -40}, {GateRange, -40}, {GateRelease, 50}});
    auto x = sine(-12, 200, 24000), q = sine(-63, 200, 72000); x.insert(x.end(), q.begin(), q.end());
    CHECK(rmsDb(run(p, x), 60000, 96000) == doctest::Approx(-103).epsilon(0.02));
}
TEST_CASE("CS04 De-ess module: loud sibilance down, the body untouched") {
    auto p = make({{DeessOn, 1}, {DeessFreq, 4000}, {DeessThresh, -40}, {DeessRange, -12}});
    // sibilance well above the corner: a 2nd-order shelf cannot reach the full range only 0.4 octave above it
    auto x = sine(-10, 10000, 48000);
    const auto body = sine(-30, 400, 48000); for (size_t i = 0; i < x.size(); ++i) x[i] += body[i];
    const auto y = run(p, x);
    auto lvl = [](const std::vector<float>& v, double f) {
        std::complex<double> a; for (size_t i = 24000; i < v.size(); ++i) a += static_cast<double>(v[i]) * std::exp(std::complex<double>(0, -2 * eqt::kPi * f * i / eqt::kFs));
        return 20 * std::log10(std::abs(a));
    };
    CHECK(lvl(y, 10000) < lvl(x, 10000) - 9.0);
    CHECK(std::abs(lvl(y, 400) - lvl(x, 400)) < 0.5);
}
TEST_CASE("CS04 Saturate module adds harmonics, Mix 0 removes them") {
    auto wet = make({{SatOn, 1}, {SatDrive, 18}}), dry = make({{SatOn, 1}, {SatDrive, 18}, {SatMix, 0}});
    CHECK(eqt::harmonicDb(wet, 200, 3, 0.3) > eqt::harmonicDb(dry, 200, 3, 0.3) + 20.0);
}
TEST_CASE("CS04 order matters: EQ boost before the compressor is squashed, after it is not") {
    const int eqFirst = indexFromOrder({1, 2, 0, 3, 4, 5}), compFirst = indexFromOrder({2, 1, 0, 3, 4, 5});  // EQ>Comp vs Comp>EQ
    auto a = make({{Order, eqFirst}, {CompOn, 1}, {CompThresh, -20}, {CompRatio, 4}, {EqMid, 12}});
    auto b = make({{Order, compFirst}, {CompOn, 1}, {CompThresh, -20}, {CompRatio, 4}, {EqMid, 12}});
    const double la = rmsDb(run(a, sine(-24, 2500, 96000)), 48000, 96000), lb = rmsDb(run(b, sine(-24, 2500, 96000)), 48000, 96000);
    CHECK(lb > la + 3.0);
}

TEST_CASE("CS04 Low lat: the limiter looks ahead by 1 sample (the smallest it can) instead of 1 ms; reported from the next prepare on; the ceiling still holds; Limit Off is unaffected") {
    CHECK(std::string(specs()[LowLat].id) == "cs04.lowlat"); CHECK(specs()[LowLat].labels == std::vector<std::string>{"Off", "On"}); CHECK(specs()[LowLat].def == 0); CHECK(specs()[LowLat].automatable); CHECK(LowLat == kNumParams - 1);
    { Processor p; p.setParam(LimitOn, 1); CHECK(p.latencySamples() == 48); p.setParam(LowLat, 1); CHECK(p.latencySamples() == 1); p.setParam(LimitOn, 0); CHECK(p.latencySamples() == 0); p.setParam(LowLat, 0); CHECK(p.latencySamples() == 0); }
    auto q = make({{LimitOn, 1}, {LimitCeiling, -6}, {LowLat, 1}}); CHECK(q.latencySamples() == 1);
    double pk = 0; for (float v : run(q, sine(0, 1000, 24000))) pk = std::max(pk, (double)std::abs(v));
    CHECK(20 * std::log10(pk) <= -6.0 + 1e-4);
    std::vector<float> click(4096, 0.0f); click[1000] = 1.0f; click[1001] = -1.0f; auto c = make({{LimitOn, 1}, {LimitCeiling, -6}, {LowLat, 1}}); pk = 0; for (float v : run(c, click)) pk = std::max(pk, (double)std::abs(v));
    CHECK(20 * std::log10(pk) <= -6.0 + 1e-4);   // an instant click is held at the ceiling too (no look-ahead: the gain drops on the sample itself)
    // the Low lat output is the signal one sample later: with the limiter idle (a quiet signal) it passes unchanged
    auto r = make({{LimitOn, 1}, {LowLat, 1}}); const auto x = sine(-30, 440, 8000); const auto y = run(r, x);
    for (size_t i = 100; i + 1 < x.size(); i += 53) CHECK(y[i + 1] == doctest::Approx(x[i]).epsilon(1e-5));
}
