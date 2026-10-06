#include "doctest.h"
#include "sw/oversample.hpp"
#include "sw/saturate.hpp"
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
