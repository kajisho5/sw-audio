// SW AUDIO core — zero-latency convolution (GT02 Cab IR): the first B taps of the kernel are convolved directly in the time domain,
// the rest by the partitioned FFT convolver (latency B) with the kernel shifted by B, so that the sum has no latency at all.
//   y[n] = sum_{k<B} h[k] x[n-k]  +  (Convolver with kernel h[B..]) delayed by B  =  sum_k h[k] x[n-k].
// A new kernel is crossfaded from the old one (head and tail together) over fadeSamples; no allocation after prepare().
#pragma once
#include "sw/convolver.hpp"
#include <algorithm>
#include <vector>

namespace sw {

class ZeroLatencyConvolver {
public:
    void prepare(int maxKernel, int block, int numCh, int fadeSamples) {
        B_ = block; nch_ = std::max(1, std::min(2, numCh)); fadeLen_ = std::max(1, fadeSamples);
        tail_.prepare(std::max(block, maxKernel - block), block, nch_);
        tail_.setFadeSamples(fadeLen_);
        headCur_.assign(static_cast<size_t>(B_), 0.0); headNext_ = headCur_;
        tailK_.assign(static_cast<size_t>(std::max(block, maxKernel - block)), 0.0);
        sz_ = 1; while (sz_ < static_cast<size_t>(2 * B_)) sz_ <<= 1;
        for (auto& c : hist_) c.assign(2 * sz_, 0.0);   // every sample is written twice (at i and i + sz_) so that the last B samples are always contiguous
        pos_ = 0; fadeLeft_ = 0;
        dry_.assign(static_cast<size_t>(B_), 0.0f);
    }
    // forget the audio (history of the direct part and of the tail convolver), keep the kernel; a running fade is finished
    void reset() {
        if (fadeLeft_ > 0) finishFade();
        for (auto& c : hist_) std::fill(c.begin(), c.end(), 0.0);
        tail_.reset(); pos_ = 0;
    }
    int latencySamples() const { return 0; }
    bool fading() const { return fadeLeft_ > 0; }
    // immediate: no crossfade (first kernel / prepare)
    void setKernel(const std::vector<double>& h, bool immediate) {
        if (fadeLeft_ > 0) finishFade();
        std::vector<double>& head = immediate ? headCur_ : headNext_;
        for (int k = 0; k < B_; ++k) head[static_cast<size_t>(k)] = static_cast<size_t>(k) < h.size() ? h[static_cast<size_t>(k)] : 0.0;
        for (size_t k = 0; k < tailK_.size(); ++k) tailK_[k] = static_cast<size_t>(B_) + k < h.size() ? h[static_cast<size_t>(B_) + k] : 0.0;
        tail_.setKernel(tailK_, immediate);
        if (!immediate) fadeLeft_ = fadeLen_;
    }
    void process(float** ch, int numCh, int n) {
        const int nch = std::min(numCh, nch_);
        for (int off = 0; off < n; off += B_) {
            const int m = std::min(B_, n - off);
            float* p[2] = {ch[0] + off, ch[nch - 1] + off};
            // history first (dry input), then the tail convolver overwrites p in place with its output
            for (int c = 0; c < nch; ++c) {
                auto& hist = hist_[static_cast<size_t>(c)];
                for (int i = 0; i < m; ++i) { const size_t at = static_cast<size_t>(pos_ + i) & (sz_ - 1); hist[at] = p[c][i]; hist[at + sz_] = p[c][i]; }
            }
            tail_.process(p, nch, m);
            for (int i = 0; i < m; ++i) {
                const double g = fadeLeft_ > 0 ? 1.0 - static_cast<double>(fadeLeft_) / fadeLen_ : 0.0;
                for (int c = 0; c < nch; ++c) {
                    const auto& hist = hist_[static_cast<size_t>(c)];
                    const double* x = hist.data() + sz_ + (static_cast<size_t>(pos_ + i) & (sz_ - 1));   // x[-k] is the sample k back
                    const double* hc = headCur_.data();
                    const double a = dotBack(hc, x, B_);
                    const double b = fadeLeft_ > 0 ? dotBack(headNext_.data(), x, B_) : 0.0;
                    p[c][i] = static_cast<float>(p[c][i] + (fadeLeft_ > 0 ? a + g * (b - a) : a));
                }
                if (fadeLeft_ > 0 && --fadeLeft_ == 0) headCur_.swap(headNext_);
            }
            pos_ = (pos_ + m) & static_cast<int>(sz_ - 1);
        }
    }

private:
    // sum_k h[k] * x[-k] (k < n): four partial sums, so that the additions do not wait for each other (a plain loop is one long chain of dependent additions; the compiler may not reorder them)
    static double dotBack(const double* h, const double* x, int n) {
        double a0 = 0, a1 = 0, a2 = 0, a3 = 0; int k = 0;
        for (; k + 4 <= n; k += 4) { a0 += h[k] * x[-k]; a1 += h[k + 1] * x[-k - 1]; a2 += h[k + 2] * x[-k - 2]; a3 += h[k + 3] * x[-k - 3]; }
        for (; k < n; ++k) a0 += h[k] * x[-k];
        return (a0 + a1) + (a2 + a3);
    }
    void finishFade() { headCur_.swap(headNext_); fadeLeft_ = 0; }
    Convolver tail_;
    int B_ = 256, nch_ = 2, fadeLen_ = 960, fadeLeft_ = 0, pos_ = 0;
    size_t sz_ = 512;
    std::vector<double> headCur_, headNext_, tailK_;
    std::vector<float> dry_;
    std::array<std::vector<double>, 2> hist_{};
};

}  // namespace sw
