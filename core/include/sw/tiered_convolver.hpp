// SW AUDIO core — zero-latency convolution of long kernels (RV04): non-uniform partitions, one channel.
//   taps [0, 128)      direct (ZeroLatencyConvolver, block 128, covers [0, 1152))
//   taps [1152, 16384) FFT convolver, block 1024, input delayed by 1152 - 1024 = 128 samples
//   taps [16384, ..)   deferred FFT convolver (sw::DeferredConvolver), block 8192, latency 2 x 8192 = 16384: no input delay; its work is spread over each block
// A tier with block B (latency L) and kernel segment [s, e) is fed the input delayed by (s - L): its own latency then makes the sum exact.
// A new kernel is loaded in steps (beginKernel / stepKernel / commitKernel) and crossfaded in over fadeSamples; no allocation after prepare().
#pragma once
#include "sw/convolver.hpp"
#include "sw/deferred_convolver.hpp"
#include "sw/zl_convolver.hpp"
#include <algorithm>
#include <vector>

namespace sw {

class TieredConvolver {
public:
    static constexpr int kB1 = 128, kEnd1 = 1152, kB2 = 1024, kEnd2 = 16384, kB3 = 8192;
    // phase 0 / 1: the FFT blocks of the two tiers (and of a second instance, for the right channel) are started half a block apart so that
    // the heavy blocks of the left and right convolvers do not fall into the same process call (the input before time 0 is silence, so nothing changes)
    void prepare(int maxKernel, int fadeSamples, int phase = 0) {
        maxK_ = std::max(maxKernel, kEnd1);
        zl_.prepare(kEnd1, kB1, 1, fadeSamples);
        has2_ = maxK_ > kEnd1; has3_ = maxK_ > kEnd2;
        if (has2_) { t2_.prepare(std::min(maxK_, kEnd2) - kEnd1, kB2, 1); t2_.setFadeSamples(fadeSamples); d2_.assign(static_cast<size_t>(kEnd1 - kB2), 0.0f); seg2_.assign(static_cast<size_t>(std::min(maxK_, kEnd2) - kEnd1), 0.0); }
        if (has3_) { t3_.prepare(maxK_ - kEnd2, kB3); t3_.setFadeSamples(fadeSamples); seg3_.assign(static_cast<size_t>(maxK_ - kEnd2), 0.0); }
        head_.assign(static_cast<size_t>(kEnd1), 0.0);
        p2_ = 0;
        scratch_.assign(256, 0.0f); s2_ = scratch_; s3_ = scratch_;
        stage_ = 0;
        if (phase != 0) {
            std::vector<float> z(256, 0.0f); float* pz[1] = {z.data()};
            if (has2_) for (int i = 0; i < 3 * kB2 / 4 / 256; ++i) t2_.process(pz, 1, 256);
            if (has3_) for (int i = 0; i < kB3 / 2 / 256; ++i) t3_.process(z.data(), 256);
        }
    }
    int latencySamples() const { return 0; }
    bool fading() const { return zl_.fading(); }
    // copy the kernel (at most maxKernel taps; `length` = taps that are not zero, -1: all) and start the step-wise transform
    void beginKernel(const std::vector<double>& h, int length = -1) {
        const int len = length < 0 ? maxK_ : length;
        for (int i = 0; i < kEnd1; ++i) head_[static_cast<size_t>(i)] = static_cast<size_t>(i) < h.size() ? h[static_cast<size_t>(i)] : 0.0;
        if (has2_) { for (size_t i = 0; i < seg2_.size(); ++i) seg2_[i] = static_cast<size_t>(kEnd1) + i < h.size() ? h[static_cast<size_t>(kEnd1) + i] : 0.0; t2_.beginKernel(seg2_, len - kEnd1); }
        if (has3_) { for (size_t i = 0; i < seg3_.size(); ++i) seg3_[i] = static_cast<size_t>(kEnd2) + i < h.size() ? h[static_cast<size_t>(kEnd2) + i] : 0.0; t3_.beginKernel(seg3_, len - kEnd2); }
        stage_ = 1;
    }
    // transforms up to maxParts partitions (all tiers together); true when the kernel is ready to commit
    bool stepKernel(int maxParts) {
        if (stage_ == 0) return true;
        bool done = true;
        if (has2_) done = t2_.stepKernel(maxParts) && done;
        if (has3_) done = t3_.stepKernel(maxParts) && done;
        if (done) stage_ = 2;
        return done;
    }
    void commitKernel(bool immediate) {
        if (stage_ != 2) return;
        zl_.setKernel(head_, immediate);
        if (has2_) t2_.commitKernel(immediate);
        if (has3_) t3_.commitKernel(immediate);
        stage_ = 0;
    }
    void setKernel(const std::vector<double>& h, bool immediate) { beginKernel(h); while (!stepKernel(1 << 20)) {} commitKernel(immediate); }
    void process(float* x, int n) {
        for (int off = 0; off < n; off += 256) {
            const int m = std::min(256, n - off);
            float* p = x + off;
            for (int i = 0; i < m; ++i) {
                s2_[static_cast<size_t>(i)] = has2_ ? delayed(d2_, p2_, p[i]) : 0.0f;
                s3_[static_cast<size_t>(i)] = has3_ ? p[i] : 0.0f;
                scratch_[static_cast<size_t>(i)] = p[i];
            }
            float* a[1] = {scratch_.data()}; zl_.process(a, 1, m);
            if (has2_) { float* b[1] = {s2_.data()}; t2_.process(b, 1, m); }
            if (has3_) t3_.process(s3_.data(), m);
            for (int i = 0; i < m; ++i) p[i] = scratch_[static_cast<size_t>(i)] + (has2_ ? s2_[static_cast<size_t>(i)] : 0.0f) + (has3_ ? s3_[static_cast<size_t>(i)] : 0.0f);
        }
    }

private:
    static float delayed(std::vector<float>& ring, size_t& pos, float x) {   // ring of length d: the value written d samples ago
        const float y = ring[pos]; ring[pos] = x; if (++pos >= ring.size()) pos = 0; return y;
    }
    ZeroLatencyConvolver zl_;
    Convolver t2_;
    DeferredConvolver t3_;
    std::vector<float> d2_, scratch_, s2_, s3_;
    std::vector<double> head_, seg2_, seg3_;
    size_t p2_ = 0;
    int maxK_ = kEnd1, stage_ = 0;
    bool has2_ = false, has3_ = false;
};

}  // namespace sw
