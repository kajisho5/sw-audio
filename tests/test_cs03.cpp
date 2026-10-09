#include "doctest.h"
#include "eq_helpers.hpp"
#include "os_helpers.hpp"
#include "cs03/cs03.hpp"
#include <cmath>
#include <vector>
using namespace sw;
using namespace sw::cs03;
namespace {
Processor make(std::vector<std::pair<int, double>> set) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(eqt::kFs, 64); p.snapToTargets(); return p; }
double rmsOut(Processor& p, double rmsIn, double f = 1000) {
    const int n = 48000 * 2; const double a = std::pow(10.0, (rmsIn + 3.0103) / 20.0);
    std::vector<float> l(n), r(n);
    for (int i = 0; i < n; ++i) l[i] = r[i] = static_cast<float>(a * std::sin(2 * eqt::kPi * f * i / eqt::kFs));
    for (int off = 0; off < n; off += 256) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, std::min(256, n - off)); }
    double s = 0; for (int i = n / 2; i < n; ++i) s += l[i] * l[i];
    return 10 * std::log10(s / (n / 2));
}
}

TEST_CASE("CS03 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(std::string(s[Gain].id) == "cs03.pre.gain"); CHECK(s[Gain].min == 0); CHECK(s[Gain].max == 60); CHECK(s[Gain].def == 30);
    CHECK(std::string(s[Impedance].id) == "cs03.pre.z"); CHECK(s[Impedance].labels == std::vector<std::string>{"Lo Z", "Hi Z"}); CHECK(s[Impedance].def == 0);
    CHECK(s[Low].numSteps() == 13); CHECK(s[Mid].steps.front() == -12); CHECK(s[High].steps.back() == 12);
    CHECK(std::string(s[Thresh].id) == "cs03.comp.thresh"); CHECK(s[Thresh].max == 10);
    CHECK(s[Ratio].min == doctest::Approx(1.2)); CHECK(s[Ratio].max == 10); CHECK(s[Ratio].def == 2); CHECK(s[Ratio].curve == Curve::Log);
    CHECK(s[Knee].labels == std::vector<std::string>{"Hard", "Soft"}); CHECK(s[Knee].def == 1);
    CHECK(std::string(s[Output].id) == "cs03.out");
}
TEST_CASE("CS03 Gain: scale 30 = 0 dB, 40 = +10 dB") {
    { auto p = make({}); CHECK(eqt::gainDb(p, 1000) == doctest::Approx(0).epsilon(0.01)); }
    { auto p = make({{Gain, 40}}); CHECK(eqt::gainDb(p, 1000) == doctest::Approx(10).epsilon(0.01)); }
}
TEST_CASE("CS03 Hi Z loads the top end and changes the saturation") {
    auto lo = make({}), hi = make({{Impedance, 1}});
    CHECK(eqt::gainDb(hi, 15000) < eqt::gainDb(lo, 15000) - 1.0);
    auto hot = make({{Gain, 60}, {Impedance, 1}}), hotLo = make({{Gain, 60}});
    CHECK(eqt::harmonicDb(hot, 200, 2, 0.02) > eqt::harmonicDb(hotLo, 200, 2, 0.02) + 6.0);  // more even harmonics on Hi Z
}
TEST_CASE("CS03 EQ: EQ06 proportional Q in 2 dB steps") {
    { auto p = make({{Mid, 12}}); CHECK(eqt::gainDb(p, 1500) == doctest::Approx(12).epsilon(0.01)); }
    { auto p = make({{Low, 12}}); CHECK(eqt::gainDb(p, 25) > 11.0); }
    { auto p = make({{High, -12}}); CHECK(eqt::gainDb(p, 20000) < -11.0); }
    auto small = make({{Mid, 2}}), big = make({{Mid, 12}});
    CHECK(eqt::gainDb(big, 3000) / 12.0 < eqt::gainDb(small, 3000) / 2.0 - 0.15);
}
TEST_CASE("CS03 compressor: 0..10 = 0..-40 dBFS, hard knee 4:1") {
    auto p = make({{Thresh, 5}, {Ratio, 4}, {Knee, 0}});
    CHECK(rmsOut(p, -10) == doctest::Approx(-20 + 10.0 / 4).epsilon(0.03));  // feed-forward: -17.5
}

// the common oversampling setting (spec 共通機能: 1x / 2x / 4x, default 2x)
TEST_CASE("CS03: the oversampling parameter is the last one, 1x / 2x / 4x, default 2x, and the transformer follows it") {
    const auto& s = specs();
    CHECK(std::string(s[Oversample].id) == "cs03.os"); CHECK(s[Oversample].steps == std::vector<double>{1, 2, 4}); CHECK(s[Oversample].def == 2.0); CHECK(Oversample == kNumParams - 1);
    auto alias = [](int os) { auto p = make({{Gain, 60}, {Oversample, double(os)}}); return ost::relDb(p, 15000, 3000, 0.05); };
    const double a1 = alias(1), a2 = alias(2), a4 = alias(4);
    INFO("15 kHz at Gain 60, alias at 3 kHz: 1x " << a1 << " dB, 2x " << a2 << " dB, 4x " << a4 << " dB");
    CHECK(a1 > -50.0); CHECK(a2 < a1 - 15.0); CHECK(ost::notWorse(a4, a2));
}
TEST_CASE("CS03: the transformer's low split (150 Hz) is at the same frequency at every oversampling setting") {
    double lo[3], hi[3]; int k = 0;
    for (int os : {1, 2, 4}) {
        auto a = make({{Gain, 60}, {Oversample, double(os)}}), b = make({{Gain, 60}, {Oversample, double(os)}});
        lo[k] = eqt::harmonicDb(a, 60, 3, 0.05); hi[k] = eqt::harmonicDb(b, 1000, 3, 0.05); ++k;
    }
    INFO("60 Hz: " << lo[0] << " / " << lo[1] << " / " << lo[2] << " dB, 1 kHz: " << hi[0] << " / " << hi[1] << " / " << hi[2] << " dB");
    for (int i : {0, 2}) { CHECK(std::abs(lo[i] - lo[1]) < 1.0); CHECK(std::abs(hi[i] - hi[1]) < 1.0); }
}
