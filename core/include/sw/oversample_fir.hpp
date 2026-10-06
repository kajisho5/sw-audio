// SW AUDIO core — linear-phase FIR oversampler (4x / 8x / 16x) for clippers (spec: MS04 は直線位相の多相 FIR)
// Prototype: Kaiser-windowed sinc, odd length factor*T+1 -> exact integer latency of T base samples (up + down).
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace sw {

class FirOversampler {
public:
    static constexpr int kTapsPerPhase = 48;

    void setup(int factor) {
        constexpr double kPi = 3.14159265358979323846;
        n_ = std::max(1, factor);
        len_ = n_ * kTapsPerPhase + 1;
        h_.assign(static_cast<size_t>(len_), 0.0);
        const double c = (len_ - 1) / 2.0, beta = 9.0;
        auto i0 = [](double x) { double s = 1, t = 1; for (int k = 1; k < 50; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
        for (int i = 0; i < len_; ++i) {
            const double t = (i - c) / n_;  // base-rate samples
            const double sinc = std::abs(t) < 1e-12 ? 1.0 : std::sin(kPi * t) / (kPi * t);
            const double r = (i - c) / c;
            h_[static_cast<size_t>(i)] = sinc * i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0(beta) / n_;  // unity DC gain at the high rate
        }
        xin_.assign(static_cast<size_t>(kTapsPerPhase + 2), 0.0);
        xpos_ = 0;
        whi_.assign(static_cast<size_t>(len_), 0.0);
        wpos_ = 0;
    }
    int factor() const { return n_; }
    int latencySamples() const { return kTapsPerPhase; }  // T/2 (up) + T/2 (down)

    // one base sample -> factor high-rate samples (zero-stuff + filter, polyphase)
    void up(double x, double* out) {
        const int J = static_cast<int>(xin_.size());
        xpos_ = (xpos_ + 1) % J;
        xin_[static_cast<size_t>(xpos_)] = x;
        for (int k = 0; k < n_; ++k) {
            double acc = 0;
            int idx = xpos_;
            for (int i = k; i < len_; i += n_) {
                acc += h_[static_cast<size_t>(i)] * xin_[static_cast<size_t>(idx)];
                idx = idx == 0 ? J - 1 : idx - 1;
            }
            out[k] = acc * n_;  // zero-stuffing gain
        }
    }
    // factor high-rate samples -> one base sample (filter + decimate, aligned to the block's first sample)
    double down(const double* in) {
        // the output uses the first sample of this block as its newest input -> integer total delay
        push(in[0]);
        double acc = 0;
        int idx = wpos_;
        for (int i = 0; i < len_; ++i) {
            acc += h_[static_cast<size_t>(i)] * whi_[static_cast<size_t>(idx)];
            idx = idx == 0 ? len_ - 1 : idx - 1;
        }
        for (int k = 1; k < n_; ++k) push(in[k]);
        return acc;
    }
    void reset() { std::fill(xin_.begin(), xin_.end(), 0.0); std::fill(whi_.begin(), whi_.end(), 0.0); }

private:
    void push(double v) { wpos_ = (wpos_ + 1) % len_; whi_[static_cast<size_t>(wpos_)] = v; }
    int n_ = 4, len_ = 1, xpos_ = 0, wpos_ = 0;
    std::vector<double> h_, xin_, whi_;
};

}  // namespace sw
