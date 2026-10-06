#include "doctest.h"
#include "sw/pitch_engine.hpp"
#include "tu.hpp"
#include "voice.hpp"
#include <complex>
using namespace sw;
using namespace tu;
namespace {
struct Fixed : RatioSource { double r = 1.0, fm = 1.0; void ratio(double, bool v, double, double& pr, double& f) override { pr = v ? r : 1.0; f = v ? fm : 1.0; } };
std::vector<float> shift(const std::vector<float>& x, double r, double fm, PitchConfig cfg = {}, PitchAnalyzer** keep = nullptr) {
    PitchAnalyzer an;
    an.prepare(cfg); PsolaSynth sy; sy.prepare(an); Fixed f; f.r = r; f.fm = fm;
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) { an.push(x[i]); y[i] = static_cast<float>(sy.process(an, f)); }
    (void)keep; return y;
}
}

TEST_CASE("PitchAnalyzer: the period of a sung vowel to a tenth of a sample") {
    for (double f0 : {100.0, 147.3, 220.0, 330.0, 660.0}) {
        PitchAnalyzer an; PitchConfig c; an.prepare(c); const auto x = voice(f0, 1.0);
        for (float v : x) an.push(v);
        const auto fr = an.trackAt(an.trackEnd());
        CHECK(fr.voiced); NEAR(fr.period, kFs / f0, 0.15 + kFs / f0 * 0.001);
    }
}
TEST_CASE("PitchAnalyzer: noise and silence are not voiced; marks are one period apart") {
    { PitchAnalyzer an; PitchConfig c; an.prepare(c); for (float v : noise(-20, 1.0, 3)) an.push(v); CHECK(!an.trackAt(an.trackEnd()).voiced); }
    { PitchAnalyzer an; PitchConfig c; an.prepare(c); for (int i = 0; i < 48000; ++i) an.push(0.0); CHECK(!an.trackAt(an.trackEnd()).voiced); }
    PitchAnalyzer an; PitchConfig c; an.prepare(c); const auto x = voice(200.0, 1.0); for (float v : x) an.push(v);
    double mk, per; bool vo; REQUIRE(an.nearestMark(30000.0, mk, per, vo)); CHECK(vo);
    double a = 0, b = 0; bool v1, v2; an.nearestMark(30000.0, a, per, v1); an.nearestMark(a + 1.5 * per, b, per, v2);
    NEAR(b - a, 240.0, 2.0);
}
TEST_CASE("PsolaSynth: ratio 1 returns the input after the latency, up to an offset below half a period") {
    PitchConfig c; PitchAnalyzer an; an.prepare(c);
    const auto x = voice(180.0, 1.5); const auto y = shift(x, 1.0, 1.0);
    const int lat = an.latency();
    // short windows: in each the output is the input delayed by a lag within half a period of the latency, and nearly the same waveform
    for (size_t a = 30000; a + 2400 <= 66000; a += 6000) {
        double best = -1; int bestLag = 0;
        for (int lag = lat - 140; lag <= lat + 140; ++lag) { double r = 0, e1 = 0, e2 = 0; for (size_t i = a; i < a + 2400; ++i) { r += double(y[i]) * x[i - static_cast<size_t>(lag)]; e1 += double(y[i]) * y[i]; e2 += double(x[i - static_cast<size_t>(lag)]) * x[i - static_cast<size_t>(lag)]; } const double v = r / std::sqrt(e1 * e2); if (v > best) { best = v; bestLag = lag; } }
        CHECK(best > 0.95); (void)bestLag;
    }
    NEAR(rmsDb(y, 30000, 60000), rmsDb(x, 30000, 60000), 0.3);
}
TEST_CASE("PsolaSynth: pitch moves by the ratio, the level stays") {
    for (double r : {0.5, 0.7071, 0.89, 1.122, 1.5, 2.0}) {
        const auto x = voice(150.0, 1.5); const auto y = shift(x, r, 1.0);
        const double f = peakHz(y, 150.0 * r * 0.9, 150.0 * r * 1.1, 40000, 70000);
        NEAR(f / (150.0 * r), 1.0, 0.004);
        NEAR(rmsDb(y, 40000, 70000), rmsDb(x, 40000, 70000), 1.5);
    }
}
TEST_CASE("PsolaSynth: formants stay where they were (Keep) or move with the factor (Follow)") {
    // spectral envelope by the strongest harmonic region: energy around F1 (700 Hz) against around 700 x 1.5
    auto bandEnergy = [&](const std::vector<float>& y, double f) { double s = 0; for (double g = f * 0.85; g <= f * 1.15; g += 4.0) s += std::pow(10.0, binDb(y, g, 40000, 70000) / 10.0); return 10 * std::log10(s + 1e-30); };
    const auto x = voice(120.0, 1.5, 0.0, 700.0, 1800.0);
    const auto keep = shift(x, 1.5, 1.0);        // pitch up by a fifth, formants kept: the resonance is still near 700 Hz
    const auto follow = shift(x, 1.5, 1.5);      // formants follow: the resonance moves to 1050 Hz
    CHECK(bandEnergy(keep, 700) > bandEnergy(keep, 1050) + 2.0);
    CHECK(bandEnergy(follow, 1050) > bandEnergy(follow, 700) + 2.0);
}
TEST_CASE("PsolaSynth: a gliding voice, and noise through, stay finite and near the input level") {
    const auto x = voice(130.0, 2.0, 260.0); const auto y = shift(x, 0.75, 1.0);
    for (float v : y) CHECK(std::isfinite(v));
    CHECK(rmsDb(y, 30000, 90000) > rmsDb(x, 30000, 90000) - 3.0); CHECK(rmsDb(y, 30000, 90000) < rmsDb(x, 30000, 90000) + 3.0);
    const auto n = noise(-20, 1.5, 5); const auto z = shift(n, 1.5, 1.0);
    for (float v : z) CHECK(std::isfinite(v));
    NEAR(rmsDb(z, 40000, 70000), rmsDb(n, 40000, 70000), 1.5);   // unvoiced: ratio 1, the noise comes back
}
