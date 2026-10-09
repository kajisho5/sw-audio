// SW AUDIO core: the stereo (SIMD pair) filter and oversampler give each channel what the single-channel ones give it;
// the in-place setupRamp skip (a ramp to where the filter already is) changes nothing; and SWINGBY's voice drive keeps its alias floor.
#include "doctest.h"
#include "in07/in07.hpp"
#include "sw/oversample.hpp"
#include "sw/simd2.hpp"
#include "sw/svf.hpp"
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace {
uint32_t lcg(uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }
double rnd(uint32_t& s) { return (lcg(s) >> 8) / 8388608.0 - 1.0; }   // -1 .. 1
}  // namespace

TEST_CASE("D2: the lanes, the arithmetic, vmin and vmax") {
    const sw::D2 a(1.5, -2.0), b(0.5, 4.0);
    CHECK((a + b).lo() == 2.0); CHECK((a + b).hi() == 2.0);
    CHECK((a - b).lo() == 1.0); CHECK((a - b).hi() == -6.0);
    CHECK((a * b).lo() == 0.75); CHECK((a * b).hi() == -8.0);
    CHECK((a / b).lo() == 3.0); CHECK((a / b).hi() == -0.5);
    CHECK(vmin(a, b).lo() == 0.5); CHECK(vmin(a, b).hi() == -2.0);
    CHECK(vmax(a, b).lo() == 1.5); CHECK(vmax(a, b).hi() == 4.0);
    CHECK(sw::D2::all(3.0).hi() == 3.0);
    CHECK(sw::D2().lo() == 0.0);
}

TEST_CASE("StereoSvf: each channel equals a single Svf with the same settings and ramps (every mode used by the synth)") {
    for (auto mode : {sw::Svf::Mode::LowPass, sw::Svf::Mode::HighPass, sw::Svf::Mode::BandPass}) {
        sw::StereoSvf st;
        sw::Svf l, r;
        uint32_t seed = 7;
        double worst = 0.0;
        for (int block = 0; block < 400; ++block) {
            const double fc = 40.0 * std::pow(2.0, 9.0 * (0.5 + 0.5 * rnd(seed))), q = 0.5 + 10.0 * (0.5 + 0.5 * rnd(seed));
            const int n = block % 3 == 0 ? 0 : 32;   // immediate and ramped updates
            st.setupRamp(mode, fc, 48000.0, q, 0.0, n);
            l.setupRamp(mode, fc, 48000.0, q, 0.0, n);
            r.setupRamp(mode, fc, 48000.0, q, 0.0, n);
            for (int i = 0; i < 32; ++i) {
                const double xl = rnd(seed), xr = rnd(seed);
                const sw::D2 y = st.process(sw::D2(xl, xr));
                const double yl = l.process(xl), yr = r.process(xr);
                worst = std::max({worst, std::fabs(y.lo() - yl), std::fabs(y.hi() - yr)});
            }
        }
        CHECK(worst < 1e-9);
    }
}

TEST_CASE("Svf: a ramp to where the filter already is changes nothing (it is skipped)") {
    sw::Svf a, b;
    a.setup(sw::Svf::Mode::LowPass, 1200.0, 48000.0, 0.9, 0.0);
    b.setup(sw::Svf::Mode::LowPass, 1200.0, 48000.0, 0.9, 0.0);
    uint32_t seed = 3;
    for (int block = 0; block < 50; ++block) {
        a.setupRamp(sw::Svf::Mode::LowPass, 1200.0, 48000.0, 0.9, 0.0, 32);   // the same target again and again
        CHECK_FALSE(a.ramping());
        for (int i = 0; i < 32; ++i) { const double x = rnd(seed); CHECK(a.process(x) == b.process(x)); }
    }
    a.setupRamp(sw::Svf::Mode::LowPass, 2400.0, 48000.0, 0.9, 0.0, 32);       // a real change still ramps
    CHECK(a.ramping());
}

TEST_CASE("StereoOversampler2xN: each channel equals the single-channel oversampler (8 and 12 coefficients)") {
    auto run = [](auto& st, auto& l, auto& r) {
        uint32_t seed = 11;
        double worst = 0.0;
        for (int i = 0; i < 20000; ++i) {
            const double xl = rnd(seed), xr = rnd(seed);
            sw::D2 up[2];
            st.up(sw::D2(xl, xr), up);
            double ul[2], ur[2];
            l.up(xl, ul); r.up(xr, ur);
            worst = std::max({worst, std::fabs(up[0].lo() - ul[0]), std::fabs(up[1].lo() - ul[1]), std::fabs(up[0].hi() - ur[0]), std::fabs(up[1].hi() - ur[1])});
            for (auto& u : up) u = u * u;   // something nonlinear in between
            for (double& u : ul) u = u * u;
            for (double& u : ur) u = u * u;
            const sw::D2 y = st.down(up);
            worst = std::max({worst, std::fabs(y.lo() - l.down(ul)), std::fabs(y.hi() - r.down(ur))});
        }
        return worst;
    };
    { sw::StereoOversampler2xN<8> st(0.06); sw::Oversampler2xN<8> l(0.06), r(0.06); CHECK(run(st, l, r) < 1e-12); }
    { sw::StereoOversampler2xN<12> st; sw::Oversampler2x l, r; CHECK(run(st, l, r) < 1e-12); }
}

namespace {
// SWINGBY: one saw copy through the layer drive, open LP12, no effects: the non-harmonic power against the harmonics (Blackman-Harris)
double voiceAliasDb(int key, double drive) {
    using namespace sw::in07;
    const double fs = 48000.0;
    const int N = 1 << 15;
    Processor p;
    p.setParam(lp(0, Wave), Saw); p.setParam(lp(0, Unison), 1); p.setParam(lp(0, FilterType), LP12); p.setParam(lp(0, Cutoff), 20000);
    p.setParam(lp(0, Resonance), 0); p.setParam(lp(0, FilterEnv), 0); p.setParam(lp(0, KeyTrack), 0); p.setParam(lp(0, Drive), drive);
    p.setParam(lp(0, AmpA), 0.5); p.setParam(lp(0, AmpS), 100); p.setParam(lp(0, VelSens), 0);
    for (int f = 0; f < kFx; ++f) p.setParam(fxOnId(f), 0);
    p.prepare(fs, 256);
    p.noteOn(key, 1.0);
    std::vector<float> l(256), r(256);
    float* c[2] = {l.data(), r.data()};
    for (int i = 0; i < 40; ++i) p.process(c, 2, 256);
    std::vector<double> x;
    while (static_cast<int>(x.size()) < N) { p.process(c, 2, 256); x.insert(x.end(), l.begin(), l.end()); }
    x.resize(static_cast<size_t>(N));
    std::vector<std::complex<double>> X(static_cast<size_t>(N));
    constexpr double kPi = 3.14159265358979323846;
    for (int i = 0; i < N; ++i) {
        const double t = 2.0 * kPi * i / (N - 1);
        X[static_cast<size_t>(i)] = x[static_cast<size_t>(i)] * (0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t));
    }
    for (int i = 1, j = 0; i < N; ++i) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(X[static_cast<size_t>(i)], X[static_cast<size_t>(j)]);
    }
    for (int len = 2; len <= N; len <<= 1) {
        const std::complex<double> w = std::polar(1.0, -2.0 * kPi / len);
        for (int i = 0; i < N; i += len) {
            std::complex<double> u = 1.0;
            for (int k = 0; k < len / 2; ++k) {
                const auto a = X[static_cast<size_t>(i + k)], b = X[static_cast<size_t>(i + k + len / 2)] * u;
                X[static_cast<size_t>(i + k)] = a + b; X[static_cast<size_t>(i + k + len / 2)] = a - b;
                u *= w;
            }
        }
    }
    const double f0 = 440.0 * std::pow(2.0, (key - 69) / 12.0);
    double harm = 0.0, other = 0.0;
    for (int b = 1; b < N / 2; ++b) {
        const double f = b * fs / N, pw = std::norm(X[static_cast<size_t>(b)]);
        const double d = std::fabs(f / f0 - std::round(f / f0)) * f0;
        (d < 6.0 * fs / N ? harm : other) += pw;
    }
    return 10.0 * std::log10(other / harm + 1e-30);
}
}  // namespace

TEST_CASE("IN07: the voice drive's alias floor (8-coefficient 2x, both channels as a pair) stays where it was measured") {
    // measured 2026-10-09 (README「IN07 の評価」): the 2x rate's own folding dominates; the lighter half-band moved it by at most 5 dB
    CHECK(voiceAliasDb(60, 18) < -80.0);
    CHECK(voiceAliasDb(96, 18) < -72.0);
    CHECK(voiceAliasDb(96, 0) < -88.0);    // no drive: the oscillator's own (minBLEP)
    CHECK(voiceAliasDb(60, 100) < -38.0);
}
