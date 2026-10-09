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
        rfft_.setup(2 * B_);
        const size_t bins = static_cast<size_t>(B_ + 1);
        cur_.assign(static_cast<size_t>(P_), std::vector<cd>(bins));
        next_ = cur_;
        stage_ = cur_;
        usedStage_ = usedCur_ = usedNext_ = P_;
        pend_.assign(static_cast<size_t>(P_) * static_cast<size_t>(B_), 0.0);
        for (auto& c : ch_) {
            c.fdl.assign(static_cast<size_t>(P_), std::vector<cd>(bins));
            c.in.assign(static_cast<size_t>(2 * B_), 0.0);
            c.out.assign(static_cast<size_t>(B_), 0.0);
            c.outNext.assign(static_cast<size_t>(B_), 0.0);
        }
        head_ = 0; pos_ = 0; fadeLeft_ = 0;
        wr_.assign(static_cast<size_t>(2 * B_), 0.0);
        acc_ = std::vector<cd>(bins);
    }
    // forget the audio (the input history and the results not played yet), keep the kernel: what comes out afterwards is silence until new input arrives. A running fade is finished (the new kernel stays).
    void reset() {
        if (fadeLeft_ > 0) { cur_.swap(next_); std::swap(usedCur_, usedNext_); fadeLeft_ = 0; }
        for (auto& c : ch_) {
            for (auto& v : c.fdl) std::fill(v.begin(), v.end(), cd(0, 0));
            std::fill(c.in.begin(), c.in.end(), 0.0); std::fill(c.out.begin(), c.out.end(), 0.0); std::fill(c.outNext.begin(), c.outNext.end(), 0.0);
        }
        head_ = 0; pos_ = 0;
    }
    void setFadeSamples(int n) { fadeLen_ = std::max(1, n); }
    int latencySamples() const { return B_; }
    bool fading() const { return fadeLeft_ > 0; }
    // immediate: replace without a crossfade (prepare / first kernel). Otherwise fade from the current kernel.
    void setKernel(const std::vector<double>& h, bool immediate) {
        beginKernel(h);
        while (!stepKernel(P_)) {}
        commitKernel(immediate);
    }
    // the same in three steps, so that a long kernel can be transformed a few partitions at a time (no stall in the audio thread):
    // beginKernel copies h; stepKernel(n) transforms up to n partitions and returns true when all are done; commitKernel starts the (cross)fade
    // length: the number of taps that are not zero (-1: all); only that many partitions are transformed and multiplied afterwards
    void beginKernel(const std::vector<double>& h, int length = -1) {
        usedStage_ = length < 0 ? P_ : std::clamp((length + B_ - 1) / B_, 1, P_);
        const size_t span = std::min(static_cast<size_t>(usedStage_) * static_cast<size_t>(B_), pend_.size());
        const size_t copy = std::min(h.size(), span);
        std::copy(h.begin(), h.begin() + static_cast<std::ptrdiff_t>(copy), pend_.begin());
        std::fill(pend_.begin() + static_cast<std::ptrdiff_t>(copy), pend_.begin() + static_cast<std::ptrdiff_t>(span), 0.0);
        partNext_ = 0;
    }
    bool stepKernel(int maxParts) {
        const int end = std::min(usedStage_, partNext_ + std::max(1, maxParts));
        for (int p = partNext_; p < end; ++p) {
            std::copy(pend_.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(p) * static_cast<size_t>(B_)), pend_.begin() + static_cast<std::ptrdiff_t>((static_cast<size_t>(p) + 1) * static_cast<size_t>(B_)), wr_.begin());
            std::fill(wr_.begin() + B_, wr_.end(), 0.0);
            rfft_.forward(wr_.data(), stage_[static_cast<size_t>(p)].data());
        }
        partNext_ = end;
        return partNext_ >= usedStage_;
    }
    void commitKernel(bool immediate) {
        if (fadeLeft_ > 0) { cur_.swap(next_); std::swap(usedCur_, usedNext_); fadeLeft_ = 0; }  // finish a running fade first
        auto& dst = immediate ? cur_ : next_;
        dst.swap(stage_);
        (immediate ? usedCur_ : usedNext_) = usedStage_;
        if (!immediate) {
            fadeLeft_ = fadeLen_;
            for (int c = 0; c < nch_; ++c) convolveInto(next_, usedNext_, ch_[static_cast<size_t>(c)], ch_[static_cast<size_t>(c)].outNext);  // current block too
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
            if (fadeLeft_ > 0 && --fadeLeft_ == 0) { cur_.swap(next_); std::swap(usedCur_, usedNext_); for (auto& s : ch_) s.out.swap(s.outNext); }
            if (++pos_ == B_) { pos_ = 0; block(nch); }
        }
    }

private:
    using cd = std::complex<double>;
    struct Ch { std::vector<std::vector<cd>> fdl; std::vector<double> in, out, outNext; };
    void convolveInto(const std::vector<std::vector<cd>>& K, int used, const Ch& s, std::vector<double>& out) {
        std::fill(acc_.begin(), acc_.end(), cd(0, 0));
        for (int p = 0; p < used; ++p) {
            const auto& X = s.fdl[static_cast<size_t>((head_ - p + P_) % P_)];
            const auto& H = K[static_cast<size_t>(p)];
            // complex multiply-accumulate on plain doubles through restrict pointers: the compiler can vectorize it (std::complex's operators and the indexing cost about 25 instructions a bin)
            double* SW_RESTRICT a = reinterpret_cast<double*>(acc_.data());
            const double* SW_RESTRICT x = reinterpret_cast<const double*>(X.data());
            const double* SW_RESTRICT h = reinterpret_cast<const double*>(H.data());
            for (int k = 0; k <= B_; ++k) {
                const double xr = x[2 * k], xi = x[2 * k + 1], hr = h[2 * k], hi = h[2 * k + 1];
                a[2 * k] += xr * hr - xi * hi; a[2 * k + 1] += xr * hi + xi * hr;
            }
        }
        rfft_.inverse(acc_.data(), wr_.data());
        for (int k = 0; k < B_; ++k) out[static_cast<size_t>(k)] = wr_[static_cast<size_t>(B_ + k)];  // overlap-save: keep the 2nd half
    }
    void block(int nch) {
        head_ = (head_ + 1) % P_;
        for (int c = 0; c < nch; ++c) {
            Ch& s = ch_[static_cast<size_t>(c)];
            rfft_.forward(s.in.data(), s.fdl[static_cast<size_t>(head_)].data());
            std::copy(s.in.begin() + B_, s.in.end(), s.in.begin());  // slide: this block becomes the next "previous"
            convolveInto(cur_, usedCur_, s, s.out);
            if (fadeLeft_ > 0) convolveInto(next_, usedNext_, s, s.outNext);
        }
    }
    int B_ = 64, P_ = 1, nch_ = 2, head_ = 0, pos_ = 0, fadeLen_ = 960, fadeLeft_ = 0;
    RealFft rfft_;
    std::vector<std::vector<cd>> cur_, next_, stage_;
    std::vector<double> pend_;
    int partNext_ = 0, usedStage_ = 1, usedCur_ = 1, usedNext_ = 1;
    std::array<Ch, 2> ch_{};
    std::vector<double> wr_;
    std::vector<cd> acc_;
};

}  // namespace sw
