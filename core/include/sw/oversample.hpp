// SW AUDIO core — 2x oversampler, polyphase IIR half-band (spec: 標準は最小位相 IIR ハーフバンド、報告遅延 0)
// Two parallel chains of first-order all-pass sections. Coefficient design follows the
// classic elliptic half-band construction (Valenzuela & Constantinides), as popularised by
// L. de Soras' HIIR. Group delay is a few samples and frequency dependent; reported latency is 0.
#pragma once
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

class Oversampler2x {
public:
    static constexpr int kCoefs = 12;
    Oversampler2x() : c_(halfband::design<kCoefs>(0.0415)) { reset(); }

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

}  // namespace sw
