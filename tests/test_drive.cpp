// Output stage shared by EQ01 / EQ03 / EQ04 (spec: 偶数次寄りの非対称ソフトクリップ1段、2× OS、音量補正つき)
#include "doctest.h"
#include "sw/drive.hpp"
#include <cmath>
#include <complex>
#include <vector>
using namespace sw;

namespace {
constexpr double kPi = 3.14159265358979323846, kFs = 48000.0;
std::vector<double> run(double drive, double f, double amp, int n = 48000) {
    DriveStage d; d.prepare(kFs, drive); d.snap();
    std::vector<double> y(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) { d.tick(); y[static_cast<size_t>(i)] = d.process(0, amp * std::sin(2 * kPi * f * i / kFs)); }
    return y;
}
// amplitude at fr over the second half (Hann window)
double binAmp(const std::vector<double>& y, double fr) {
    const int n = static_cast<int>(y.size()); std::complex<double> a; double w = 0;
    for (int i = n / 2; i < n; ++i) { const double h = 0.5 - 0.5 * std::cos(2 * kPi * (i - n / 2) / (n / 2 - 1)); a += h * y[static_cast<size_t>(i)] * std::exp(std::complex<double>(0, -2 * kPi * fr * i / kFs)); w += h; }
    return 2 * std::abs(a) / w;
}
double harmDb(const std::vector<double>& y, double f, int k) { return 20 * std::log10(binAmp(y, f * k) / binAmp(y, f) + 1e-12); }
double rmsDb(const std::vector<double>& y) { double s = 0; const size_t h = y.size() / 2; for (size_t i = h; i < y.size(); ++i) s += y[i] * y[i]; return 10 * std::log10(s / static_cast<double>(y.size() - h)); }
}  // namespace

TEST_CASE("drive stage makes even-order harmonics (asymmetric), and 2nd leads 3rd") {
    const auto y = run(10, 1000, 0.18);
    const double h2 = harmDb(y, 1000, 2), h3 = harmDb(y, 1000, 3);
    CHECK(h2 > -30.0);             // a symmetric clipper would leave 2nd at the noise floor (< -100 dB)
    CHECK(h2 > h3 + 3.0);          // "偶数次寄り"
    CHECK(h2 < -12.0);             // a tone-shaper, not a fuzz
}
TEST_CASE("drive stage: even harmonics grow with Drive and vanish at Drive 0 for normal levels") {
    const double h2_0 = harmDb(run(0, 1000, 0.18), 1000, 2), h2_5 = harmDb(run(5, 1000, 0.18), 1000, 2), h2_10 = harmDb(run(10, 1000, 0.18), 1000, 2);
    CHECK(h2_0 < -55.0);           // +6 dBFS headroom (decision): Drive 0 stays clean
    CHECK(h2_5 > h2_0 + 15.0); CHECK(h2_10 > h2_5 + 5.0);
}
TEST_CASE("drive stage: small-signal gain is unity at any Drive") {
    for (double drive : {0.0, 5.0, 10.0}) CHECK(20 * std::log10(binAmp(run(drive, 1000, 0.001), 1000) / 0.001) == doctest::Approx(0.0).epsilon(0.0).scale(0.1));
}
TEST_CASE("drive stage: loudness stays within 1 dB across Drive for a -18 dBFS RMS tone") {
    const double a = 0.1778 * std::sqrt(2.0);  // -18 dBFS RMS
    const double ref = rmsDb(run(0, 1000, a));
    for (double drive : {2.0, 5.0, 10.0}) CHECK(std::abs(rmsDb(run(drive, 1000, a)) - ref) < 1.0);
}
TEST_CASE("drive stage: the asymmetry leaves no DC offset") {
    const auto y = run(10, 1000, 0.3, 96000);
    double m = 0; for (size_t i = y.size() / 2; i < y.size(); ++i) m += y[i]; m /= static_cast<double>(y.size() / 2);
    CHECK(std::abs(m) < 2e-3);
}
TEST_CASE("drive stage: 2x oversampling keeps the 3rd harmonic of 15 kHz from folding to 3 kHz") {
    const auto y = run(10, 15000, 0.3);
    CHECK(20 * std::log10(binAmp(y, 3000) / binAmp(y, 15000)) < -60.0);
}
TEST_CASE("drive stage output is bounded and finite") {
    DriveStage d; d.prepare(kFs, 10); d.snap();
    for (int i = 0; i < 1000; ++i) { d.tick(); const double v = d.process(0, (i & 1) ? 100.0 : -100.0); CHECK(std::isfinite(v)); CHECK(std::abs(v) < 1.0); }
}
