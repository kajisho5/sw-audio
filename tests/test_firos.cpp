#include "doctest.h"
#include "sw/oversample_fir.hpp"
#include <cmath>
#include <complex>
#include <vector>
using namespace sw;
namespace {
const double kPi = 3.14159265358979323846;
double toneDb(const std::vector<double>& y, double f, double fs) {
    const size_t n0 = y.size() / 2, n = y.size() - n0; std::complex<double> acc; double w = 0;
    for (size_t i = 0; i < n; ++i) { double h = 0.5 - 0.5 * std::cos(2 * kPi * i / (n - 1)); acc += h * y[n0 + i] * std::exp(std::complex<double>(0, -2 * kPi * f * (n0 + i) / fs)); w += h; }
    return 20 * std::log10(2 * std::abs(acc) / w);
}
}
TEST_CASE("linear-phase FIR oversampler: integer latency, flat to 20 kHz, images rejected") {
    for (int f : {4, 8, 16}) {
        FirOversampler os; os.setup(f);
        CHECK(os.latencySamples() == FirOversampler::kTapsPerPhase);
        // impulse through up+down comes out at exactly the reported latency
        std::vector<double> imp;
        std::vector<double> up(static_cast<size_t>(f));
        for (int i = 0; i < 200; ++i) { os.up(i == 0 ? 1.0 : 0.0, up.data()); imp.push_back(os.down(up.data())); }
        size_t peak = 0; for (size_t i = 1; i < imp.size(); ++i) if (std::abs(imp[i]) > std::abs(imp[peak])) peak = i;
        CHECK(static_cast<int>(peak) == os.latencySamples());
        for (double tone : {1000.0, 20000.0}) {
            FirOversampler o2; o2.setup(f);
            std::vector<double> y; std::vector<double> u(static_cast<size_t>(f));
            for (int i = 0; i < 24000; ++i) { o2.up(std::sin(2 * kPi * tone * i / 48000.0), u.data()); y.push_back(o2.down(u.data())); }
            CHECK(std::abs(toneDb(y, tone, 48000.0)) < (tone < 10000 ? 0.01 : 0.25));
        }
        FirOversampler o3; o3.setup(f);
        std::vector<double> hi; std::vector<double> u(static_cast<size_t>(f));
        for (int i = 0; i < 12000; ++i) { o3.up(std::sin(2 * kPi * 10000.0 * i / 48000.0), u.data()); for (double v : u) hi.push_back(v); }
        const double fsHi = 48000.0 * f;
        CHECK(toneDb(hi, 10000.0, fsHi) > -0.05);
        CHECK(toneDb(hi, 38000.0, fsHi) < -80.0);  // first image
    }
}
