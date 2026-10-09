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

// 4x / 8x / 16x as a cascade of Oversampler2x stages (a minimum-phase IIR half-band per octave, no reported delay): the same up / down interface as FirOversampler (oversample_fir.hpp),
// so a clipper can switch between the linear-phase FIR and this. Stage k runs at 2^k times the base rate; every stage keeps the memory of its own stream.
class IirOversampler {
public:
    static constexpr int kMaxStages = 4;   // 16x
    void setup(int factor) { stages_ = factor >= 16 ? 4 : factor >= 8 ? 3 : factor >= 4 ? 2 : 1; n_ = 1 << stages_; reset(); }
    int factor() const { return n_; }
    int latencySamples() const { return 0; }
    // one base sample -> factor high-rate samples
    void up(double x, double* out) {
        out[0] = x; int cnt = 1; double tmp[16];
        for (int s = 0; s < stages_; ++s) { for (int i = 0; i < cnt; ++i) st_[static_cast<size_t>(s)].up(out[i], &tmp[2 * i]); cnt *= 2; for (int i = 0; i < cnt; ++i) out[i] = tmp[i]; }
    }
    // factor high-rate samples -> one base sample
    double down(const double* in) {
        double buf[16]; for (int i = 0; i < n_; ++i) buf[i] = in[i];
        int cnt = n_;
        for (int s = stages_ - 1; s >= 0; --s) { for (int i = 0; i < cnt / 2; ++i) buf[i] = st_[static_cast<size_t>(s)].down(&buf[2 * i]); cnt /= 2; }
        return buf[0];
    }
    void reset() { for (auto& s : st_) s.reset(); }

private:
    int stages_ = 2, n_ = 4;
    std::array<Oversampler2x, kMaxStages> st_{};
};

// A nonlinear stage at 1x / 2x / 4x (/ 8x) of the base rate (spec, common function "オーバーサンプリング": 1x / 2x / 4x, default 2x; the standard setting is the minimum-phase IIR
// half-band, no reported delay). process(x, f) calls f on every sample of the oversampled signal, in order, factor times per input sample, and returns the base-rate result;
// f is the stage's own work (a waveshaper, a filter with its state, ...): whatever it keeps in time (filter coefficients, a DC blocker) must be computed for rate() = factor * fs.
// 8x is not a setting of the common parameter: a product that wants one more octave for a harsh shape (SA06's Fold and Fuzz) asks for it itself.
// One channel: the caller keeps one OsSwitch per channel (and per stage). Newly enabled half-bands start from zero; 1x is f itself.
class OsSwitch {
public:
    OsSwitch() = default;
    explicit OsSwitch(int factor) : n_(snap(factor)) {}
    static int snap(double v) { return v >= 6.0 ? 8 : v >= 3.0 ? 4 : v >= 1.5 ? 2 : 1; }   // any number -> the nearest of 1, 2, 4, 8 (a tie goes up)
    void setFactor(int f) {
        f = snap(f);
        if (f == n_) return;
        for (int k = levels(n_); k < levels(f); ++k) st_[static_cast<size_t>(k)].reset();   // a half-band that was off starts from zero
        n_ = f;
    }
    int factor() const { return n_; }
    double rate(double fs) const { return fs * n_; }
    void reset() { for (auto& s : st_) s.reset(); }
    template <class F> double process(double x, F&& f) {
        if (n_ == 1) return f(x);
        double u[2];
        st_[0].up(x, u);
        if (n_ == 2) { u[0] = f(u[0]); u[1] = f(u[1]); return st_[0].down(u); }
        if (n_ == 4) {
            for (double& s : u) {   // 4x: the second half-band works on the 2x stream, one sample at a time
                double v[2];
                st_[1].up(s, v);
                v[0] = f(v[0]); v[1] = f(v[1]);
                s = st_[1].down(v);
            }
            return st_[0].down(u);
        }
        for (double& s : u) s = run(1, s, f);   // 8x
        return st_[0].down(u);
    }

private:
    static int levels(int factor) { return factor >= 8 ? 3 : factor >= 4 ? 2 : factor >= 2 ? 1 : 0; }
    template <class F> double run(int level, double x, F& f) {   // one sample at the rate of stage `level`'s input -> back, through the stages above it
        if (level == 3) return f(x);
        double u[2];
        st_[static_cast<size_t>(level)].up(x, u);
        u[0] = run(level + 1, u[0], f); u[1] = run(level + 1, u[1], f);
        return st_[static_cast<size_t>(level)].down(u);
    }
    int n_ = 2;
    std::array<Oversampler2x, 3> st_{};
};

}  // namespace sw
