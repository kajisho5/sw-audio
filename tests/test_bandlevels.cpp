#include "doctest.h"
#include "sw/bandlevels.hpp"
#include "tu.hpp"
using namespace sw;
using namespace tu;
TEST_CASE("ThirdOctaveAnalyzer: a sine lands in its band at its mean-square level") {
    ThirdOctaveAnalyzer a; a.setup(kFs, 4096, 0.5);
    const auto x = sine(-20, 4, 1000); a.process(x.data(), static_cast<int>(x.size()));
    const int b = 17; NEAR(ThirdOctaveAnalyzer::centerHz(b), 1000.0, 1e-9);
    NEAR(a.levelDb(b), -20.0, 0.6);                     // RMS -20 dBFS = mean square -20 dB
    CHECK(a.levelDb(b + 2) < -60.0); CHECK(a.levelDb(b - 2) < -60.0);
}
TEST_CASE("ThirdOctaveAnalyzer: pink noise reads flat across the bands, white noise rises 1 dB per band") {
    // pink noise by filtering white noise with a 1/f approximation (Voss-McCartney: octave-spaced sample-and-hold sources)
    Gauss g(5); std::vector<float> pink(static_cast<size_t>(60 * kFs)); double rows[16] = {}; 
    for (size_t i = 0; i < pink.size(); ++i) { double sum = 0; for (int r = 0; r < 16; ++r) { if (r == 0 || (i % (1u << r)) == 0) rows[r] = g.gauss(); sum += rows[r]; } pink[i] = static_cast<float>(0.05 * sum / 4.0); }
    ThirdOctaveAnalyzer a; a.setup(kFs, 4096, 5.0); a.process(pink.data(), static_cast<int>(pink.size()));
    double lo = 1e9, hi = -1e9; for (int b = 10; b < 29; ++b) { lo = std::min(lo, a.levelDb(b)); hi = std::max(hi, a.levelDb(b)); }   // 100 Hz .. 12.7 kHz
    CHECK(hi - lo < 6.0);
    const auto w = noise(-30, 30, 2); ThirdOctaveAnalyzer c; c.setup(kFs, 4096, 5.0); c.process(w.data(), static_cast<int>(w.size()));
    NEAR(c.levelDb(26) - c.levelDb(14), 12.0, 1.5);   // 12 bands = 4 octaves, white noise: +3 dB per octave
}
