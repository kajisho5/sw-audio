// SW AUDIO core — rolling 1/3-octave band levels (31 bands, 25 Hz .. 20 kHz) from a mono stream: Hann-windowed FFT every N/2 samples,
// band power (mean square, Parseval-scaled so a full-scale sine reads -3.01 dB) smoothed over `smooth` seconds in the power domain.
// Used by the spectrum-aware EVO functions (SA05 Auto fill, EQ-like analysers, MT products).
#pragma once
#include "sw/fft.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace sw {

class ThirdOctaveAnalyzer {
public:
    static constexpr int kBands = 31;
    static double centerHz(int i) { return 1000.0 * std::pow(2.0, (i - 17) / 3.0); }   // 25 Hz (i = 0) .. 20 kHz (i = 30)
    void setup(double fs, int fftSize = 4096, double smoothSeconds = 1.0) {
        fs_ = fs; n_ = fftSize; fft_.setup(n_);
        win_.resize(static_cast<size_t>(n_)); double sw2 = 0;
        for (int i = 0; i < n_; ++i) { win_[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / n_); sw2 += win_[static_cast<size_t>(i)] * win_[static_cast<size_t>(i)]; }
        scale_ = 2.0 / (static_cast<double>(n_) * sw2);
        buf_.assign(static_cast<size_t>(n_), 0.0); ring_.assign(static_cast<size_t>(n_), 0.0);
        frame_.assign(static_cast<size_t>(n_), 0.0);
        alpha_ = 1.0 - std::exp(-(n_ / 2.0) / (smoothSeconds * fs));
        lo_.fill(0); hi_.fill(0);
        for (int b = 0; b < kBands; ++b) {
            const double lo = centerHz(b) / std::pow(2.0, 1.0 / 6.0), hi = centerHz(b) * std::pow(2.0, 1.0 / 6.0);
            lo_[static_cast<size_t>(b)] = std::max(1, static_cast<int>(std::ceil(lo * n_ / fs))); hi_[static_cast<size_t>(b)] = std::min(n_ / 2, static_cast<int>(std::ceil(hi * n_ / fs)) - 1);
        }
        reset();
    }
    void reset() { std::fill(ring_.begin(), ring_.end(), 0.0); pos_ = 0; sinceFrame_ = 0; frames_ = 0; power_.fill(0.0); }
    void process(const float* x, int n) { for (int i = 0; i < n; ++i) push(x[i]); }
    void push(double x) {
        ring_[static_cast<size_t>(pos_)] = x; pos_ = (pos_ + 1) % n_;
        if (++sinceFrame_ >= n_ / 2) { sinceFrame_ = 0; analyse(); }
    }
    // smoothed band level in dB (mean-square power); -200 for an empty band / nothing measured yet
    double levelDb(int band) const { const double p = power_[static_cast<size_t>(band)]; return p > 1e-20 ? 10.0 * std::log10(p) : -200.0; }
    int frames() const { return frames_; }

private:
    void analyse() {
        for (int i = 0; i < n_; ++i) buf_[static_cast<size_t>(i)] = ring_[static_cast<size_t>((pos_ + i) % n_)] * win_[static_cast<size_t>(i)];
        fft_.forward(buf_);
        for (int b = 0; b < kBands; ++b) {
            double p = 0; for (int k = lo_[static_cast<size_t>(b)]; k <= hi_[static_cast<size_t>(b)]; ++k) p += std::norm(buf_[static_cast<size_t>(k)]);
            p *= scale_;
            double& s = power_[static_cast<size_t>(b)];
            s = frames_ == 0 ? p : s + alpha_ * (p - s);
        }
        ++frames_;
    }
    double fs_ = 48000.0, scale_ = 1.0, alpha_ = 0.1;
    int n_ = 4096, pos_ = 0, sinceFrame_ = 0, frames_ = 0;
    Fft fft_;
    std::vector<double> win_, ring_, frame_;
    std::vector<std::complex<double>> buf_;
    std::array<int, kBands> lo_{}, hi_{};
    std::array<double, kBands> power_{};
};

}  // namespace sw
