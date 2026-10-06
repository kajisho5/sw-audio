// SW AUDIO core — short-time Fourier transform frame: sqrt-Hann analysis and synthesis windows, overlap-add, in place on 1 or 2 channels.
// Latency = N samples (the output sample for input time t is complete once the last frame that covers it has been added: t + N - 1 at the latest).
// hop must divide N (N/4 gives a perfect reconstruction with the square-root Hann pair: the product is a Hann window, the sum of its overlaps N/(2 hop) is divided out).
// The handler gets the half spectrum (bins 0 .. N/2) of every channel once per hop and may change it; the upper half is rebuilt as the conjugate mirror. No allocation after prepare().
#pragma once
#include "sw/fft.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace sw {

class Stft {
public:
    struct Handler {
        virtual ~Handler() = default;
        // spec[c][0 .. nbins-1], nbins = N/2 + 1
        virtual void frame(std::complex<double>* const* spec, int nch, int nbins) = 0;
    };
    void prepare(int n, int hop, int nch) {
        n_ = n; hop_ = hop; nch_ = std::min(nch, 2); fft_.setup(n);
        win_.resize(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) win_[static_cast<size_t>(i)] = std::sqrt(0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / n));
        scale_ = 2.0 * hop / n;
        for (int c = 0; c < 2; ++c) { in_[c].assign(static_cast<size_t>(n), 0.0f); oa_[c].assign(static_cast<size_t>(2 * n), 0.0f); buf_[c].assign(static_cast<size_t>(n), {0.0, 0.0}); }
        reset();
    }
    void reset() { for (int c = 0; c < 2; ++c) { std::fill(in_[c].begin(), in_[c].end(), 0.0f); std::fill(oa_[c].begin(), oa_[c].end(), 0.0f); } wpos_ = 0; t_ = 0; }
    int latency() const { return n_; }
    int size() const { return n_; }
    // in place; nch is the channel count of ch (1 or 2); with nch == 1 the handler sees one channel
    void process(float** ch, int nch, int num, Handler& h) {
        const int nc = std::min(nch, nch_);
        const int nbins = n_ / 2 + 1;
        std::complex<double>* sp[2] = {buf_[0].data(), buf_[1].data()};
        for (int i = 0; i < num; ++i) {
            for (int c = 0; c < nc; ++c) in_[c][static_cast<size_t>(wpos_)] = ch[c][i];
            wpos_ = (wpos_ + 1) % n_; ++t_;
            if (t_ >= n_ && t_ % hop_ == 0) {
                // frame start index (absolute) = t_ - n_
                for (int c = 0; c < nc; ++c) {
                    auto& b = buf_[c];
                    for (int k = 0; k < n_; ++k) b[static_cast<size_t>(k)] = {win_[static_cast<size_t>(k)] * in_[c][static_cast<size_t>((wpos_ + k) % n_)], 0.0};
                    fft_.forward(b);
                }
                h.frame(sp, nc, nbins);
                for (int c = 0; c < nc; ++c) {
                    auto& b = buf_[c];
                    b[0] = {b[0].real(), 0.0}; b[static_cast<size_t>(n_ / 2)] = {b[static_cast<size_t>(n_ / 2)].real(), 0.0};
                    for (int k = 1; k < n_ / 2; ++k) b[static_cast<size_t>(n_ - k)] = std::conj(b[static_cast<size_t>(k)]);
                    fft_.inverse(b);
                    const int64_t start = t_ - n_;
                    for (int k = 0; k < n_; ++k) oa_[c][static_cast<size_t>((start + k) % (2 * n_))] += static_cast<float>(scale_ * win_[static_cast<size_t>(k)] * b[static_cast<size_t>(k)].real());
                }
            }
            // the output for input index t_-1-n_ ... emitted now (delay n_)
            const int64_t p = t_ - 1 - n_;
            for (int c = 0; c < nc; ++c) {
                if (p < 0) { ch[c][i] = 0.0f; continue; }
                float& o = oa_[c][static_cast<size_t>(p % (2 * n_))];
                ch[c][i] = o; o = 0.0f;
            }
        }
    }

private:
    int n_ = 2048, hop_ = 512, nch_ = 2, wpos_ = 0;
    int64_t t_ = 0;
    double scale_ = 0.5;
    Fft fft_;
    std::vector<double> win_;
    std::vector<float> in_[2], oa_[2];
    std::vector<std::complex<double>> buf_[2];
};

}  // namespace sw
