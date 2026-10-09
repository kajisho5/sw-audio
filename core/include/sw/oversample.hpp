// SW AUDIO core — 2x oversampler, polyphase IIR half-band (spec: 標準は最小位相 IIR ハーフバンド、報告遅延 0)
// Two parallel chains of first-order all-pass sections. Coefficient design follows the
// classic elliptic half-band construction (Valenzuela & Constantinides), as popularised by
// L. de Soras' HIIR. Group delay is a few samples and frequency dependent; reported latency is 0.
#pragma once
#include "sw/simd2.hpp"
#include <array>
#include <cmath>

namespace sw {

namespace halfband {
inline double ipow(double x, int n) { double r = 1; while (n-- > 0) r *= x; return r; }

// coefficients for `n` all-pass sections with normalised transition bandwidth `tbw` (0..0.5)
template <int N> std::array<double, N> design(double tbw) {
    constexpr double kPi = 3.14159265358979323846;
    double k = std::tan((1 - tbw * 2) * kPi / 4); k *= k;
    const double kksqrt = std::pow(1 - k * k, 0.25);
    const double e = 0.5 * (1 - kksqrt) / (1 + kksqrt);
    const double e2 = e * e, e4 = e2 * e2;
    const double q = e * (1 + e4 * (2 + e4 * (15 + 150 * e4)));
    const int order = N * 2 + 1;
    std::array<double, N> c{};
    for (int idx = 0; idx < N; ++idx) {
        const int cc = idx + 1;
        double num = 0, den = 0, term;
        int i = 0, sgn = 1;
        do { term = ipow(q, i * (i + 1)) * std::sin((i * 2 + 1) * cc * kPi / order) * sgn; num += term; sgn = -sgn; ++i; } while (std::fabs(term) > 1e-100);
        i = 1; sgn = -1;
        do { term = ipow(q, i * i) * std::cos(i * 2 * cc * kPi / order) * sgn; den += term; sgn = -sgn; ++i; } while (std::fabs(term) > 1e-100);
        num *= std::pow(q, 0.25);
        den += 0.5;
        const double ww = num / den, wwsq = ww * ww;
        const double x = std::sqrt((1 - wwsq * k) * (1 - wwsq / k)) / (1 + wwsq);
        c[static_cast<size_t>(idx)] = (1 - x) / (1 + x);
    }
    return c;
}
}  // namespace halfband

// N all-pass coefficients (N/2 sections per branch) and the transition band tbw (of the 2x rate). The standard one, Oversampler2x:
// 12 coefficients, tbw 0.0415 (stop band about -150 dB from 26 kHz at 48 kHz, pass band to 22 kHz). Lighter ones trade the stop band.
template <int N>
class Oversampler2xN {
public:
    static constexpr int kCoefs = N;
    Oversampler2xN() : Oversampler2xN(0.0415) {}
    explicit Oversampler2xN(double tbw) : c_(halfband::design<kCoefs>(tbw)) { reset(); }

    // one input sample -> two output samples at 2x rate
    void up(double x, double out[2]) {
        double a = x, b = x;
        chains(upMem_, a, b);
        out[0] = a; out[1] = b;
    }
    // two samples at 2x rate -> one output sample
    double down(const double in[2]) {
        double a = in[1], b = in[0];
        chains(downMem_, a, b);
        return 0.5 * (a + b);
    }
    void reset() { upMem_ = {}; downMem_ = {}; }

private:
    struct Mem { std::array<double, kCoefs> x{}, y{}; };
    void chains(Mem& m, double& a, double& b) const {
        for (int i = 0; i < kCoefs; i += 2) {
            const size_t ia = static_cast<size_t>(i), ib = ia + 1;
            const double ta = m.x[ia]; m.x[ia] = a; a = (a - m.y[ia]) * c_[ia] + ta; m.y[ia] = a;
            if (ib < kCoefs) { const double tb = m.x[ib]; m.x[ib] = b; b = (b - m.y[ib]) * c_[ib] + tb; m.y[ib] = b; }
        }
    }
    std::array<double, kCoefs> c_;
    Mem upMem_, downMem_;
};
using Oversampler2x = Oversampler2xN<12>;

// the same 2x oversampler for two channels at once (left and right side by side in one SIMD pair)
template <int N>
class StereoOversampler2xN {
public:
    static constexpr int kCoefs = N;
    StereoOversampler2xN() : StereoOversampler2xN(0.0415) {}
    explicit StereoOversampler2xN(double tbw) {
        const auto c = halfband::design<kCoefs>(tbw);
        for (int i = 0; i < kCoefs; ++i) c_[static_cast<size_t>(i)] = D2::all(c[static_cast<size_t>(i)]);
        reset();
    }
    void up(D2 x, D2 out[2]) { D2 a = x, b = x; chains(upMem_, a, b); out[0] = a; out[1] = b; }
    D2 down(const D2 in[2]) { D2 a = in[1], b = in[0]; chains(downMem_, a, b); return D2::all(0.5) * (a + b); }
    void reset() { upMem_ = Mem{}; downMem_ = Mem{}; }
    void copyLeftToRight() {
        for (Mem* m : {&upMem_, &downMem_})
            for (int i = 0; i < kCoefs; ++i) {
                const size_t k = static_cast<size_t>(i);
                m->x[k] = D2(m->x[k].lo(), m->x[k].lo()); m->y[k] = D2(m->y[k].lo(), m->y[k].lo());
            }
    }

private:
    struct Mem { std::array<D2, kCoefs> x{}, y{}; };
    void chains(Mem& m, D2& a, D2& b) const {
        for (int i = 0; i < kCoefs; i += 2) {
            const size_t ia = static_cast<size_t>(i), ib = ia + 1;
            const D2 ta = m.x[ia]; m.x[ia] = a; a = (a - m.y[ia]) * c_[ia] + ta; m.y[ia] = a;
            if (ib < kCoefs) { const D2 tb = m.x[ib]; m.x[ib] = b; b = (b - m.y[ib]) * c_[ib] + tb; m.y[ib] = b; }
        }
    }
    std::array<D2, kCoefs> c_{};
    Mem upMem_, downMem_;
};

}  // namespace sw
