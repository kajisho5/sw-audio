#include "doctest.h"
#include "eq_helpers.hpp"
#include "cs01/cs01.hpp"
#include <cmath>
#include <vector>
using namespace sw;
using namespace sw::cs01;
namespace {
Processor make(std::vector<std::pair<int, double>> set) { Processor p; p.setParam(Drive, 0); for (auto& s : set) p.setParam(s.first, s.second); p.prepare(eqt::kFs, 256); p.snapToTargets(); return p; }
double rmsOut(Processor& p, double rmsIn, double f = 1000, std::vector<float>* rightOut = nullptr, double rightAmp = -1) {
    const int n = 48000 * 2; const double a = std::pow(10.0, (rmsIn + 3.0103) / 20.0);
    std::vector<float> l(n), r(n);
    for (int i = 0; i < n; ++i) { l[i] = static_cast<float>(a * std::sin(2 * eqt::kPi * f * i / eqt::kFs)); r[i] = rightAmp < 0 ? l[i] : static_cast<float>(rightAmp * std::sin(2 * eqt::kPi * 440 * i / eqt::kFs)); }
    for (int off = 0; off < n; off += 256) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, std::min(256, n - off)); }
    if (rightOut) *rightOut = r;
    double s = 0; for (int i = n / 2; i < n; ++i) s += l[i] * l[i];
    return 10 * std::log10(s / (n / 2));
}
}

TEST_CASE("CS01 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(std::string(s[Drive].id) == "cs01.pre.drive"); CHECK(s[Drive].def == 2);
    CHECK(s[Hpf].steps == std::vector<double>{0, 50, 80, 160}); CHECK(s[Hpf].def == 0);
    CHECK(std::string(s[High].id) == "cs01.eq.high"); CHECK(s[High].max == 16);
    CHECK(s[MidFreq].steps == std::vector<double>{700, 1600, 3200, 4800}); CHECK(s[MidFreq].def == 1600);
    CHECK(s[Mid].max == 18); CHECK(s[Low].min == -16);
    CHECK(std::string(s[Thresh].id) == "cs01.comp.thresh"); CHECK(s[Thresh].max == 10); CHECK(s[Thresh].def == 0);
    CHECK(s[Ratio].steps == std::vector<double>{2, 4, 8, 20}); CHECK(s[Ratio].def == 4);
    CHECK(s[Release].min == 50); CHECK(s[Release].max == 1500); CHECK(s[Release].def == 200); CHECK(s[Release].skew == 2);
    CHECK(s[Order].labels == std::vector<std::string>{"EQ first", "Comp first"});
    CHECK(std::string(s[Mix].id) == "cs01.comp.mix"); CHECK(s[Mix].def == 100);
    CHECK(std::string(s[Output].id) == "cs01.out"); CHECK(s[Link].def == 1);
}
TEST_CASE("CS01 EQ uses the EQ04 circuit: fixed 60 Hz / 12 kHz shelves, stepped mid bell, 18 dB/oct HPF") {
    { auto p = make({{Mid, 18}}); CHECK(eqt::gainDb(p, 1600) == doctest::Approx(18).epsilon(0.01)); }
    { auto p = make({{High, 16}}); CHECK(eqt::gainDb(p, 20000) > 14.5); }
    { auto p = make({{Low, 16}}); CHECK(eqt::gainDb(p, 25) > 15.5); }
    { auto p = make({{Hpf, 80}}); CHECK(eqt::gainDb(p, 80) == doctest::Approx(-3.01).epsilon(0.03)); }
}
TEST_CASE("CS01 feedback compressor: -20 dBFS threshold, 4:1 on a steady tone") {
    auto p = make({{Thresh, 5}});
    CHECK(rmsOut(p, -10) == doctest::Approx(-20 + 10.0 / 4).epsilon(0.03));
}
TEST_CASE("CS01 Mix 0 bypasses the compressor only") {
    auto p = make({{Thresh, 5}, {Mix, 0}}), ref = make({});  // same strip with the compressor idle
    CHECK(rmsOut(p, -10) == doctest::Approx(rmsOut(ref, -10)).epsilon(0.002));
}
TEST_CASE("CS01 Order: EQ first feeds the boost into the compressor") {
    auto eqFirst = make({{Thresh, 5}, {Mid, 12}}), compFirst = make({{Thresh, 5}, {Mid, 12}, {Order, 1}});
    CHECK(rmsOut(compFirst, -20, 1600) > rmsOut(eqFirst, -20, 1600) + 3.0);
}
TEST_CASE("CS01 Link: a loud left channel pulls the right one down only when linked") {
    std::vector<float> r1, r2;
    auto on = make({{Thresh, 5}}); rmsOut(on, -6, 1000, &r1, 0.01);
    auto off = make({{Thresh, 5}, {Link, 0}}); rmsOut(off, -6, 1000, &r2, 0.01);
    auto rms = [](const std::vector<float>& x) { double s = 0; for (size_t i = x.size() / 2; i < x.size(); ++i) s += x[i] * x[i]; return 10 * std::log10(s / (x.size() / 2)); };
    CHECK(rms(r1) < rms(r2) - 6.0);
}
TEST_CASE("CS01 pre: the iron saturates lows more than highs") {
    auto lo = make({{Drive, 10}}), hi = make({{Drive, 10}});
    CHECK(eqt::harmonicDb(lo, 50, 3, 0.3) > eqt::harmonicDb(hi, 2000, 3, 0.3) + 6.0);
}
