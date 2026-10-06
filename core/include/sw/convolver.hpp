// SW AUDIO core — uniformly partitioned overlap-save FFT convolution (latency = one block),
// with a crossfade between the old and the new kernel (default 960 samples = 20 ms at 48 kHz).
#pragma once
#include "sw/fft.hpp"
#include <algorithm>
#include <array>
#include <complex>
#include <vector>

namespace sw {

class Convolver {
public:
    void prepare(int maxKernel, int block, int numCh) {
        B_ = block; P_ = (maxKernel + block - 1) / block; nch_ = std::max(1, std::min(2, numCh));
        fft_.setup(2 * B_);
        const size_t bins = static_cast<size_t>(B_ + 1);
        cur_.assign(static_cast<size_t>(P_), std::vector<cd>(bins));
        next_ = cur_;
        for (auto& c : ch_) {
            c.fdl.assign(static_cast<size_t>(P_), std::vector<cd>(bins));
            c.in.assign(static_cast<size_t>(2 * B_), 0.0);
            c.out.assign(static_cast<size_t>(B_), 0.0);
            c.outNext.assign(static_cast<size_t>(B_), 0.0);
        }
        head_ = 0; pos_ = 0; fadeLeft_ = 0;
        work_.assign(static_cast<size_t>(2 * B_), cd(0, 0));
        acc_ = acc2_ = std::vector<cd>(bins);
    }
    void setFadeSamples(int n) { fadeLen_ = std::max(1, n); }
    int latencySamples() const { return B_; }
    bool fading() const { return fadeLeft_ > 0; }
    // immediate: replace without a crossfade (prepare / first kernel). Otherwise fade from the current kernel.
    void setKernel(const std::vector<double>& h, bool immediate) {
        if (fadeLeft_ > 0) { cur_.swap(next_); fadeLeft_ = 0; }  // finish a running fade first
        auto& dst = immediate ? cur_ : next_;
        for (int p = 0; p < P_; ++p) {
            std::fill(work_.begin(), work_.end(), cd(0, 0));
            for (int n = 0; n < B_; ++n) {
                const size_t i = static_cast<size_t>(p * B_ + n);
                if (i < h.size()) work_[static_cast<size_t>(n)] = h[i];
            }
            fft_.forward(work_);
            for (int k = 0; k <= B_; ++k) dst[static_cast<size_t>(p)][static_cast<size_t>(k)] = work_[static_cast<size_t>(k)];
        }
        if (!immediate) {
            fadeLeft_ = fadeLen_;
            for (int c = 0; c < nch_; ++c) convolveInto(next_, ch_[static_cast<size_t>(c)], ch_[static_cast<size_t>(c)].outNext);  // current block too
        }
    }
    void process(float** ch, int numCh, int n) {
        const int nch = std::min(numCh, nch_);
        for (int i = 0; i < n; ++i) {
            const double g = fadeLeft_ > 0 ? 1.0 - static_cast<double>(fadeLeft_) / fadeLen_ : 0.0;
            for (int c = 0; c < nch; ++c) {
                Ch& s = ch_[static_cast<size_t>(c)];
                s.in[static_cast<size_t>(B_ + pos_)] = ch[c][i];
                const double y = s.out[static_cast<size_t>(pos_)];
                ch[c][i] = static_cast<float>(fadeLeft_ > 0 ? y + g * (s.outNext[static_cast<size_t>(pos_)] - y) : y);
            }
            if (fadeLeft_ > 0 && --fadeLeft_ == 0) { cur_.swap(next_); for (auto& s : ch_) s.out.swap(s.outNext); }
            if (++pos_ == B_) { pos_ = 0; block(nch); }
        }
    }

private:
    using cd = std::complex<double>;
    struct Ch { std::vector<std::vector<cd>> fdl; std::vector<double> in, out, outNext; };
    void convolveInto(const std::vector<std::vector<cd>>& K, const Ch& s, std::vector<double>& out) {
        std::fill(acc_.begin(), acc_.end(), cd(0, 0));
        for (int p = 0; p < P_; ++p) {
            const auto& X = s.fdl[static_cast<size_t>((head_ - p + P_) % P_)];
            const auto& H = K[static_cast<size_t>(p)];
            for (int k = 0; k <= B_; ++k) acc_[static_cast<size_t>(k)] += X[static_cast<size_t>(k)] * H[static_cast<size_t>(k)];
        }
        for (int k = 0; k <= B_; ++k) work_[static_cast<size_t>(k)] = acc_[static_cast<size_t>(k)];
        for (int k = 1; k < B_; ++k) work_[static_cast<size_t>(2 * B_ - k)] = std::conj(acc_[static_cast<size_t>(k)]);
        fft_.inverse(work_);
        for (int k = 0; k < B_; ++k) out[static_cast<size_t>(k)] = work_[static_cast<size_t>(B_ + k)].real();  // overlap-save: keep the 2nd half
    }
    void block(int nch) {
        head_ = (head_ + 1) % P_;
        for (int c = 0; c < nch; ++c) {
            Ch& s = ch_[static_cast<size_t>(c)];
            for (int k = 0; k < 2 * B_; ++k) work_[static_cast<size_t>(k)] = s.in[static_cast<size_t>(k)];
            fft_.forward(work_);
            auto& X = s.fdl[static_cast<size_t>(head_)];
            for (int k = 0; k <= B_; ++k) X[static_cast<size_t>(k)] = work_[static_cast<size_t>(k)];
            std::copy(s.in.begin() + B_, s.in.end(), s.in.begin());  // slide: this block becomes the next "previous"
            convolveInto(cur_, s, s.out);
            if (fadeLeft_ > 0) convolveInto(next_, s, s.outNext);
        }
    }
    int B_ = 64, P_ = 1, nch_ = 2, head_ = 0, pos_ = 0, fadeLen_ = 960, fadeLeft_ = 0;
    Fft fft_;
    std::vector<std::vector<cd>> cur_, next_;
    std::array<Ch, 2> ch_{};
    std::vector<cd> work_, acc_, acc2_;
};

}  // namespace sw
