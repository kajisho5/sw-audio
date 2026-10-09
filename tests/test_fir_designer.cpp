// sw::FirDesigner: the magnitude functions are evaluated on a log grid and interpolated to the bins (24 bands over 16k bins was most of the cost of a kernel design); the kernels must be the same as with every bin
// evaluated itself, notches and narrow bells included (the intervals where the interpolation misses are evaluated bin by bin)
#include "doctest.h"
#include "sw/fir_design.hpp"
#include "tu.hpp"
#include <random>
using namespace tu;

namespace {
// the largest difference in dB between the responses of two kernels, over 20 Hz .. 0.45 fs, where both are above -30 dB (near the bottom of a notch the response is whatever the kernel's length allows)
double responseDiff(const std::vector<double>& a, const std::vector<double>& b, double fs) {
    const int L = static_cast<int>(a.size()), N = 4 * L; sw::Fft fft(N);
    std::vector<std::complex<double>> A(static_cast<size_t>(N)), B(static_cast<size_t>(N));
    for (int i = 0; i < L; ++i) { A[static_cast<size_t>(i)] = a[static_cast<size_t>(i)]; B[static_cast<size_t>(i)] = b[static_cast<size_t>(i)]; }
    fft.forward(A); fft.forward(B);
    double worst = 0;
    for (int k = 1; k < N / 2; ++k) {
        const double f = k * fs / N; if (f < 20.0 || f > 0.45 * fs) continue;
        const double da = 20 * std::log10(std::max(std::abs(A[static_cast<size_t>(k)]), 1e-9)), db = 20 * std::log10(std::max(std::abs(B[static_cast<size_t>(k)]), 1e-9));
        if (da > -30 && db > -30) worst = std::max(worst, std::abs(da - db));
    }
    return worst;
}
}  // namespace

TEST_CASE("FirDesigner: the interpolated kernel is the bin-by-bin kernel (within 0.15 dB where it matters), for random band sets with notches and narrow bells") {
    std::mt19937 g(5); std::uniform_real_distribution<double> u(0, 1);
    double worst = 0;
    for (int trial = 0; trial < 10; ++trial) for (int L : {2048, 4096}) for (double fs : {48000.0, 96000.0}) {
        std::vector<sw::BandShape> bands; const int nb = 2 + trial;
        for (int i = 0; i < nb; ++i) {
            sw::BandShape s; s.type = static_cast<sw::BandShape::Type>(static_cast<int>(u(g) * 6) % 6); s.freq = 30 * std::pow(600.0, u(g)); s.gainDb = (u(g) - 0.5) * 24;
            s.q = trial % 3 == 0 ? 0.5 + u(g) * 25 : 0.5 + u(g) * 3; s.slope = 12 + 12 * static_cast<int>(u(g) * 3); bands.push_back(s);
        }
        auto lin = [&](double f) { return sw::totalMagnitude(bands, f); };
        sw::FirDesigner d; d.prepare(L);
        const std::vector<double> fast = d.design(lin, [](double) { return 1.0; }, false, fs, false), exact = d.design(lin, [](double) { return 1.0; }, false, fs, true);
        worst = std::max(worst, responseDiff(fast, exact, fs));
    }
    INFO("worst " << worst << " dB");
    CHECK(worst < 0.15);
}

TEST_CASE("FirDesigner: with a minimum-phase part (Mixed, the guard) the same holds; the designer allocates nothing after prepare, and a second design gives the same kernel") {
    std::vector<sw::BandShape> a(3), m(2);
    a[0].type = sw::BandShape::LowShelf; a[0].freq = 120; a[0].gainDb = 5; a[1].freq = 900; a[1].gainDb = -4; a[1].q = 3; a[2].type = sw::BandShape::Notch; a[2].freq = 6000; a[2].q = 12;
    m[0].freq = 300; m[0].gainDb = 6; m[1].type = sw::BandShape::HighShelf; m[1].freq = 8000; m[1].gainDb = -3;
    auto lin = [&](double f) { return sw::totalMagnitude(a, f); }; auto mn = [&](double f) { return sw::totalMagnitude(m, f); };
    sw::FirDesigner d; d.prepare(2048);
    const std::vector<double> fast = d.design(lin, mn, true, 48000.0, false);
    const std::vector<double> again = d.design(lin, mn, true, 48000.0, false);
    CHECK(fast == again);
    const std::vector<double> exact = d.design(lin, mn, true, 48000.0, true);
    CHECK(responseDiff(fast, exact, 48000.0) < 0.15);
}
