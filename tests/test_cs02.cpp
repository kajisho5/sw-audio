#include "doctest.h"
#include "eq_helpers.hpp"
#include "cs02/cs02.hpp"
#include "sw/text.hpp"
#include <cmath>
#include <random>
#include <vector>
using namespace sw;
using namespace sw::cs02;
namespace {
Processor make(std::vector<std::pair<int, double>> set) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(eqt::kFs, 256); p.snapToTargets(); return p; }
std::vector<float> run(Processor& p, std::vector<float> l, std::vector<float>* rOut = nullptr, const std::vector<float>* rIn = nullptr) {
    std::vector<float> r = rIn ? *rIn : l;
    for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); }
    if (rOut) *rOut = r;
    return l;
}
std::vector<float> sine(double rmsDb, double f, int n) { const double a = std::pow(10.0, (rmsDb + 3.0103) / 20); std::vector<float> x(static_cast<size_t>(n)); for (int i = 0; i < n; ++i) x[static_cast<size_t>(i)] = static_cast<float>(a * std::sin(2 * eqt::kPi * f * i / eqt::kFs)); return x; }
double rmsDb(const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += x[i] * x[i]; return 10 * std::log10(s / (b - a) + 1e-30); }
}

TEST_CASE("CS02 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(std::string(s[Ratio].id) == "cs02.comp.ratio"); CHECK(s[Ratio].def == 1); CHECK(std::string(s[Ratio].maxLabel) == "Max");
    CHECK(s[Thresh].min == -20); CHECK(s[Thresh].max == 10); CHECK(s[Thresh].def == 10); CHECK(s[Thresh].reversed);
    CHECK(s[Release].min == doctest::Approx(0.1)); CHECK(s[Release].max == 4); CHECK(s[Release].def == doctest::Approx(0.3)); CHECK(s[Release].skew == 2);
    CHECK(std::string(s[GateThresh].id) == "cs02.gate.thresh"); CHECK(s[GateThresh].def == -30); CHECK(s[GateRange].max == 40); CHECK(s[GateRange].def == 0);
    CHECK(s[Hpf].steps == std::vector<double>{0, 40, 80, 160, 350}); CHECK(s[Lpf].labels == std::vector<std::string>{"Off", "12 kHz", "8 kHz", "4 kHz"});
    CHECK(std::string(s[Hf].id) == "cs02.eq.hf"); CHECK(s[Lf].min == -15);
    CHECK(s[Route].labels == std::vector<std::string>{"Dyn to EQ", "EQ to Dyn"});
    CHECK(std::string(s[Fader].id) == "cs02.fader"); CHECK(s[Link].def == 1);
}
TEST_CASE("fader law: 0 dB sits at 75 %, the bottom is Off, the top +10 dB") {
    const auto& f = specs()[Fader];
    CHECK(f.toNorm(0.0) == doctest::Approx(0.75)); CHECK(f.toValue(1.0) == doctest::Approx(10.0));
    CHECK(formatValue(f, f.toValue(0.0)) == "Off"); CHECK(f.def == 0.0);
    for (int i = 1; i <= 100; ++i) CHECK(f.toNorm(f.toValue(i / 100.0)) == doctest::Approx(i / 100.0));
}
TEST_CASE("CS02 at its defaults is transparent") {
    auto p = make({});
    std::mt19937 rng(1); std::normal_distribution<double> nd(0, 0.2); std::vector<float> x(8192); for (auto& v : x) v = static_cast<float>(nd(rng));
    const auto y = run(p, x);
    for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == doctest::Approx(x[i]).epsilon(1e-6));
}
TEST_CASE("CS02 compressor: 0 dB threshold = -18 dBFS, 4:1; Max is infinite") {
    auto p = make({{Thresh, 0}, {Ratio, 4}});
    CHECK(rmsDb(run(p, sine(-8, 1000, 48000)), 24000, 48000) == doctest::Approx(-8 - 7.5).epsilon(0.02));
    auto m = make({{Thresh, 0}, {Ratio, 20}});
    CHECK(rmsDb(run(m, sine(-8, 1000, 48000)), 24000, 48000) == doctest::Approx(-18).epsilon(0.03));
}
TEST_CASE("CS02 gate: the quiet tail is pulled down by the range") {
    auto p = make({{GateThresh, -30}, {GateRange, 40}});
    auto x = sine(-12, 200, 24000), q = sine(-63, 200, 72000); x.insert(x.end(), q.begin(), q.end());
    const auto y = run(p, x);
    CHECK(rmsDb(y, 60000, 96000) == doctest::Approx(-63 - 40).epsilon(0.02));
    CHECK(rmsDb(y, 4800, 24000) == doctest::Approx(-12).epsilon(0.01));
}
TEST_CASE("CS02 EQ and filters") {
    { auto p = make({{Hmf, 15}}); CHECK(eqt::gainDb(p, 3000) == doctest::Approx(15).epsilon(0.01)); }
    { auto p = make({{Lmf, -15}}); CHECK(eqt::gainDb(p, 600) == doctest::Approx(-15).epsilon(0.01)); }
    { auto p = make({{Lf, 15}}); CHECK(eqt::gainDb(p, 25) > 14.0); }
    { auto p = make({{Hf, 15}}); CHECK(eqt::gainDb(p, 20000) > 14.0); }
    { auto p = make({{Hpf, 80}}); CHECK(eqt::gainDb(p, 80) == doctest::Approx(-3.01).epsilon(0.03)); }
    { auto p = make({{Lpf, 8000}}); CHECK(eqt::gainDb(p, 8000) == doctest::Approx(-3.01).epsilon(0.03)); }
}
TEST_CASE("CS02 Route: EQ to Dyn compresses the boost") {
    auto dynFirst = make({{Thresh, 0}, {Ratio, 4}, {Hmf, 15}}), eqFirst = make({{Thresh, 0}, {Ratio, 4}, {Hmf, 15}, {Route, 1}});
    const double a = rmsDb(run(dynFirst, sine(-25, 3000, 48000)), 24000, 48000), b = rmsDb(run(eqFirst, sine(-25, 3000, 48000)), 24000, 48000);
    CHECK(a == doctest::Approx(-10).epsilon(0.02)); CHECK(b < a - 3.0);
}
TEST_CASE("CS02 fader: Off is silence, +10 dB at the top") {
    auto off = make({{Fader, specs()[Fader].toValue(0.0)}});
    CHECK(rmsDb(run(off, sine(-10, 1000, 4800)), 2400, 4800) < -200);
    auto top = make({{Fader, 10}});
    CHECK(rmsDb(run(top, sine(-20, 1000, 9600)), 4800, 9600) == doctest::Approx(-10).epsilon(0.01));
}
TEST_CASE("CS02 Link") {
    auto run2 = [](double link) { auto p = make({{Thresh, 0}, {Ratio, 8}, {Link, link}}); std::vector<float> r; const auto rin = sine(-40, 440, 48000); run(p, sine(-6, 1000, 48000), &r, &rin); return rmsDb(r, 24000, 48000); };
    CHECK(run2(1) < run2(0) - 6.0);
}
