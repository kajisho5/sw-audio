#include "doctest.h"
#include "sw/oversample.hpp"
#include "sw/saturate.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>
using namespace sw;
namespace {
const double kPi = 3.14159265358979323846;
// amplitude of a frequency component over the last part of a signal (Hann-windowed DFT)
double amp(const std::vector<double>& x, double f, double fs) {
    const size_t n0 = x.size() / 2, n = x.size() - n0;
    std::complex<double> acc; double wsum = 0;
    for (size_t i = 0; i < n; ++i) {
        double w = 0.5 - 0.5 * std::cos(2 * kPi * i / (n - 1));
        acc += w * x[n0 + i] * std::exp(std::complex<double>(0, -2 * kPi * f * (n0 + i) / fs));
        wsum += w;
    }
    return 2 * std::abs(acc) / wsum;
}
double dB(double a) { return 20 * std::log10(a); }
}

TEST_CASE("2x up then down keeps passband level (1 kHz and 16 kHz at 48 kHz)") {
    for (double f : {1000.0, 16000.0}) {
        Oversampler2x os;
        std::vector<double> out;
        for (int i = 0; i < 48000; ++i) {
            double up[2];
            os.up(std::sin(2 * kPi * f * i / 48000.0), up);
            out.push_back(os.down(up));
        }
        CHECK(dB(amp(out, f, 48000.0)) == doctest::Approx(0.0).epsilon(f < 10000 ? 0.01 : 0.05));
    }
}
TEST_CASE("upsampling rejects the image of a 16 kHz tone by 90 dB") {
    Oversampler2x os;
    std::vector<double> up2;
    for (int i = 0; i < 48000; ++i) {
        double up[2];
        os.up(std::sin(2 * kPi * 16000.0 * i / 48000.0), up);
        up2.push_back(up[0]); up2.push_back(up[1]);
    }
    const double sig = amp(up2, 16000.0, 96000.0), img = amp(up2, 32000.0, 96000.0);
    CHECK(dB(img / sig) < -90.0);
}
TEST_CASE("downsampling rejects a 40 kHz component (would alias to 8 kHz) by 90 dB") {
    Oversampler2x os;
    std::vector<double> out;
    for (int i = 0; i < 48000; ++i) {
        double in2[2] = {std::sin(2 * kPi * 40000.0 * (2 * i) / 96000.0), std::sin(2 * kPi * 40000.0 * (2 * i + 1) / 96000.0)};
        out.push_back(os.down(in2));
    }
    CHECK(dB(amp(out, 8000.0, 48000.0)) < -90.0);
}
TEST_CASE("saturator keeps small-signal gain at unity for any drive") {
    for (double driveDb : {0.0, 9.0, 18.0}) {
        Saturator s; s.setDriveDb(driveDb);
        CHECK(s.process(0.01) == doctest::Approx(0.01).epsilon(0.006));  // within 0.05 dB
    }
}
TEST_CASE("saturator adds more 3rd harmonic as drive increases") {
    auto h3 = [](double driveDb) {
        Saturator s; s.setDriveDb(driveDb);
        std::vector<double> y;
        for (int i = 0; i < 48000; ++i) y.push_back(s.process(0.5 * std::sin(2 * kPi * 1000.0 * i / 48000.0)));
        return amp(y, 3000.0, 48000.0) / amp(y, 1000.0, 48000.0);
    };
    CHECK(h3(18.0) > h3(0.0) * 4);
}
TEST_CASE("saturator output is bounded") {
    Saturator s; s.setDriveDb(18.0);
    CHECK(std::abs(s.process(100.0)) <= 1.0);
    CHECK(std::isfinite(s.process(1e30)));
}

// ---- OsSwitch: a nonlinear stage at 1x / 2x / 4x (spec 共通機能「オーバーサンプリング」: 1x／2x／4x、既定 2x、標準は最小位相 IIR、報告遅延 0)
namespace {
double clip(double x) { return std::clamp(4.0 * x, -0.5, 0.5); }   // a hard clipper: every odd harmonic, 1/k
std::vector<double> runClip(int factor, double f0, int n = 48000) {
    OsSwitch os; os.setFactor(factor);
    std::vector<double> y;
    for (int i = 0; i < n; ++i) y.push_back(os.process(0.5 * std::sin(2 * kPi * f0 * i / 48000.0), clip));
    return y;
}
}
TEST_CASE("OsSwitch: factor 1 is the function itself; 2, 4 and 8 call it that many times per sample") {
    OsSwitch os;
    CHECK(os.factor() == 2);   // the default
    for (int f : {1, 2, 4, 8}) {
        os.setFactor(f); os.reset();
        int calls = 0; double last = 0;
        for (int i = 0; i < 100; ++i) last = os.process(0.1 * i, [&](double x) { ++calls; return x; });
        CHECK(calls == 100 * f);
        CHECK(os.factor() == f);
        if (f == 1) CHECK(last == 9.9);
    }
    os.setFactor(3); CHECK(os.factor() == 4);    // anything else lands on the nearest setting (a tie goes up)
    os.setFactor(5); CHECK(os.factor() == 4);
    os.setFactor(6); CHECK(os.factor() == 8);
    os.setFactor(16); CHECK(os.factor() == 8);
    os.setFactor(0); CHECK(os.factor() == 1);
    CHECK(OsSwitch::snap(1.4) == 1); CHECK(OsSwitch::snap(1.6) == 2); CHECK(OsSwitch::snap(2.9) == 2); CHECK(OsSwitch::snap(3.1) == 4); CHECK(OsSwitch::snap(5.9) == 4); CHECK(OsSwitch::snap(6.1) == 8);
    CHECK(os.rate(48000.0) == 48000.0); os.setFactor(4); CHECK(os.rate(48000.0) == 192000.0); os.setFactor(8); CHECK(os.rate(48000.0) == 384000.0);
    CHECK(OsSwitch(4).factor() == 4); CHECK(OsSwitch(8).factor() == 8);
}
TEST_CASE("OsSwitch: a linear stage keeps its level at every factor (1 kHz, 16 kHz)") {
    for (int fac : {1, 2, 4, 8}) for (double f : {1000.0, 16000.0}) {
        OsSwitch os; os.setFactor(fac);
        std::vector<double> y;
        for (int i = 0; i < 48000; ++i) y.push_back(os.process(std::sin(2 * kPi * f * i / 48000.0), [](double x) { return x; }));
        CHECK(dB(amp(y, f, 48000.0)) == doctest::Approx(0.0).epsilon(f < 10000 ? 0.01 : 0.05));
    }
}
TEST_CASE("OsSwitch: more oversampling folds less of a hard clipper back into the band") {
    // 15 kHz: the 3rd harmonic (45 kHz) folds to 3 kHz at 1x; 13 kHz: the 9th (117 kHz) folds to 5 kHz at 1x and at 2x, not at 4x
    auto rel = [](int fac, double f0, double bin) { return dB(amp(runClip(fac, f0, 96000), bin, 48000.0) / amp(runClip(fac, f0, 96000), f0, 48000.0)); };
    const double a1 = rel(1, 15000.0, 3000.0), a2 = rel(2, 15000.0, 3000.0), a4 = rel(4, 15000.0, 3000.0);
    INFO("15 kHz, alias at 3 kHz re the fundamental: 1x " << a1 << " dB, 2x " << a2 << " dB, 4x " << a4 << " dB");
    CHECK(a1 > -15.0);
    CHECK(a2 < a1 - 20.0);
    CHECK(a4 < a2 - 8.0);
    const double b2 = rel(2, 13000.0, 5000.0), b4 = rel(4, 13000.0, 5000.0);
    INFO("13 kHz, alias at 5 kHz: 2x " << b2 << " dB, 4x " << b4 << " dB");
    CHECK(b2 > -30.0);
    CHECK(b4 < b2 - 40.0);
}
TEST_CASE("OsSwitch: changing the factor while running stays finite, and 1x is again exactly the function") {
    OsSwitch os;
    const int seq[] = {2, 1, 2, 4, 8, 2, 1, 8, 4, 1};
    int n = 0; double maxd = 0;
    for (int f : seq) {
        os.setFactor(f);
        for (int i = 0; i < 4800; ++i, ++n) {
            const double x = 0.4 * std::sin(2 * kPi * 3000.0 * n / 48000.0), y = os.process(x, clip);
            REQUIRE(std::isfinite(y));
            if (f == 1 && i >= 1) maxd = std::max(maxd, std::abs(y - clip(x)));
        }
    }
    CHECK(maxd == 0.0);
}

TEST_CASE("OsSwitch: 8x takes the clipper's 13 kHz -> 5 kHz alias out further than 4x does (cascade of three half-bands)") {
    auto rel = [](int fac) { return dB(amp(runClip(fac, 13000.0, 96000), 5000.0, 48000.0) / amp(runClip(fac, 13000.0, 96000), 13000.0, 48000.0)); };
    const double a4 = rel(4), a8 = rel(8);
    INFO("13 kHz, alias at 5 kHz: 4x " << a4 << " dB, 8x " << a8 << " dB");
    CHECK(a8 < a4 + 3.0);
    CHECK(a8 < -60.0);
}
