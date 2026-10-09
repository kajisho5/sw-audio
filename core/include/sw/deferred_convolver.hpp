// SW AUDIO core — one-channel uniformly partitioned FFT convolution with the work spread over the block (latency 2 x block).
//   sw::Convolver does a block's whole convolution (forward FFT, every partition, inverse FFT) in the process call where the block ends: for a long
//   kernel with big blocks that is a spike of several milliseconds. Here the forward FFT is done at the block boundary, the partition products are done
//   a few per process call during the next block (finished within its first half), the inverse FFT after them, and the result is used one block later.
//   Kernel g, block B: output y[n] = sum_k g[k] x[n - 2B - k]. A new kernel is crossfaded in (the new kernel's result for the block being played is
//   computed at once, which is a one-off cost at the commit).
#pragma once
#include "sw/fft.hpp"
#include <algorithm>
#include <complex>
#include <vector>

namespace sw {

class DeferredConvolver {
public:
    void prepare(int maxKernel, int block) {
        B_ = block; P_ = std::max(1, (maxKernel + block - 1) / block);
        rfft_.setup(2 * B_);
        const size_t bins = static_cast<size_t>(B_ + 1);
        cur_.assign(static_cast<size_t>(P_), std::vector<cd>(bins)); next_ = cur_; stage_ = cur_;
        fdl_.assign(static_cast<size_t>(P_), std::vector<cd>(bins));
        in_.assign(static_cast<size_t>(2 * B_), 0.0);
        out_.assign(static_cast<size_t>(B_), 0.0); pend_ = outN_ = pendN_ = out_;
        wr_.assign(static_cast<size_t>(2 * B_), 0.0);
        pendK_.assign(static_cast<size_t>(P_) * static_cast<size_t>(B_), 0.0);
        jobC_.acc.assign(bins, cd(0, 0)); jobN_ = jobC_; scratch_.assign(bins, cd(0, 0));
        head_ = 0; pos_ = 0; fadeLeft_ = 0; usedStage_ = usedCur_ = usedNext_ = P_;
        jobC_.state = jobN_.state = Job::Idle;
    }
    // forget the audio (input history, jobs under way, results not played yet), keep the kernel; a running fade is finished
    void reset() {
        if (fadeLeft_ > 0) finishFade();
        jobC_.state = jobN_.state = Job::Idle;
        for (auto& v : fdl_) std::fill(v.begin(), v.end(), cd(0, 0));
        for (auto* v : {&in_, &out_, &pend_, &outN_, &pendN_}) std::fill(v->begin(), v->end(), 0.0);
        head_ = 0; pos_ = 0;
    }
    int latencySamples() const { return 2 * B_; }
    void setFadeSamples(int n) { fadeLen_ = std::max(1, n); }
    bool fading() const { return fadeLeft_ > 0; }
    void beginKernel(const std::vector<double>& h, int length = -1) {
        usedStage_ = length < 0 ? P_ : std::clamp((length + B_ - 1) / B_, 1, P_);
        const size_t span = std::min(static_cast<size_t>(usedStage_) * static_cast<size_t>(B_), pendK_.size());
        const size_t copy = std::min(h.size(), span);
        std::copy(h.begin(), h.begin() + static_cast<std::ptrdiff_t>(copy), pendK_.begin());
        std::fill(pendK_.begin() + static_cast<std::ptrdiff_t>(copy), pendK_.begin() + static_cast<std::ptrdiff_t>(span), 0.0);
        partNext_ = 0;
    }
    bool stepKernel(int maxParts) {
        const int end = std::min(usedStage_, partNext_ + std::max(1, maxParts));
        for (int p = partNext_; p < end; ++p) {
            std::copy(pendK_.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(p) * static_cast<size_t>(B_)), pendK_.begin() + static_cast<std::ptrdiff_t>((static_cast<size_t>(p) + 1) * static_cast<size_t>(B_)), wr_.begin());
            std::fill(wr_.begin() + B_, wr_.end(), 0.0);
            rfft_.forward(wr_.data(), stage_[static_cast<size_t>(p)].data());
        }
        partNext_ = end;
        return partNext_ >= usedStage_;
    }
    void commitKernel(bool immediate) {
        if (fadeLeft_ > 0) finishFade();
        if (immediate) {
            cur_.swap(stage_); usedCur_ = usedStage_;
            jobC_.state = Job::Idle; std::fill(out_.begin(), out_.end(), 0.0); std::fill(pend_.begin(), pend_.end(), 0.0);
        } else {
            next_.swap(stage_); usedNext_ = usedStage_;
            // the block being played was computed from the spectra up to head_ - 1: the new kernel's version of it, at once
            computeNow(next_, usedNext_, (head_ + P_ - 1) % P_, outN_);
            startJob(jobN_, head_);   // and the block being computed (spectra up to head_)
            fadeLeft_ = fadeLen_;
        }
    }
    void process(float* x, int n) {
        advanceJobs(n);
        for (int i = 0; i < n; ++i) {
            in_[static_cast<size_t>(B_ + pos_)] = x[i];
            double y = out_[static_cast<size_t>(pos_)];
            if (fadeLeft_ > 0) { const double g = 1.0 - static_cast<double>(fadeLeft_) / fadeLen_; y += g * (outN_[static_cast<size_t>(pos_)] - y); if (--fadeLeft_ == 0) finishFade(); }
            x[i] = static_cast<float>(y);
            if (++pos_ == B_) boundary();
        }
    }

private:
    using cd = std::complex<double>;
    struct Job { enum State { Idle, Mac, Inverse, Done } state = Idle; int part = 0, head = 0; std::vector<cd> acc; };
    // acc += X * H over the bins 0..B: plain doubles through restrict pointers (std::complex's operators and the vector indexing cost about 25 instructions a bin; this loop the compiler can vectorize)
    static void mac(std::vector<cd>& acc, const std::vector<cd>& X, const std::vector<cd>& H, int B) {
        double* SW_RESTRICT a = reinterpret_cast<double*>(acc.data());
        const double* SW_RESTRICT x = reinterpret_cast<const double*>(X.data());
        const double* SW_RESTRICT h = reinterpret_cast<const double*>(H.data());
        for (int k = 0; k <= B; ++k) {
            const double xr = x[2 * k], xi = x[2 * k + 1], hr = h[2 * k], hi = h[2 * k + 1];
            a[2 * k] += xr * hr - xi * hi; a[2 * k + 1] += xr * hi + xi * hr;
        }
    }
    void inverse(const std::vector<cd>& acc, std::vector<double>& out) {
        rfft_.inverse(acc.data(), wr_.data());
        for (int k = 0; k < B_; ++k) out[static_cast<size_t>(k)] = wr_[static_cast<size_t>(B_ + k)];   // overlap-save: the second half
    }
    void computeNow(const std::vector<std::vector<cd>>& K, int used, int head, std::vector<double>& out) {
        std::fill(jobC_.acc.begin(), jobC_.acc.end(), cd(0, 0));   // (jobC_'s accumulator is free to use while we are not in the middle of its MAC: use a local if it is)
        std::vector<cd>& a = scratch_;   // (sized in prepare(): this runs on the audio thread)
        std::fill(a.begin(), a.end(), cd(0, 0));
        for (int p = 0; p < used; ++p) mac(a, fdl_[static_cast<size_t>((head - p + P_) % P_)], K[static_cast<size_t>(p)], B_);
        inverse(a, out);
    }
    void startJob(Job& j, int head) { j.state = Job::Mac; j.part = 0; j.head = head; std::fill(j.acc.begin(), j.acc.end(), cd(0, 0)); }
    // advance one job: up to `parts` partition products, or the inverse FFT
    void stepJob(Job& j, const std::vector<std::vector<cd>>& K, int used, std::vector<double>& out, int parts) {
        if (j.state == Job::Mac) {
            const int end = std::min(used, j.part + parts);
            for (int p = j.part; p < end; ++p) mac(j.acc, fdl_[static_cast<size_t>((j.head - p + P_) % P_)], K[static_cast<size_t>(p)], B_);
            j.part = end;
            if (j.part >= used) j.state = Job::Inverse;
        } else if (j.state == Job::Inverse) {
            inverse(j.acc, out); j.state = Job::Done;
        }
    }
    void advanceJobs(int n) {
        const int budget = std::max(1, static_cast<int>(std::ceil(static_cast<double>(usedCur_) * n / (0.4 * B_))));
        stepJob(jobC_, cur_, usedCur_, pend_, budget);
        if (fadeLeft_ > 0) stepJob(jobN_, next_, usedNext_, pendN_, std::max(1, static_cast<int>(std::ceil(static_cast<double>(usedNext_) * n / (0.4 * B_)))));
    }
    void finishJob(Job& j, const std::vector<std::vector<cd>>& K, int used, std::vector<double>& out) {
        while (j.state == Job::Mac || j.state == Job::Inverse) stepJob(j, K, used, out, 1 << 20);
    }
    void finishFade() {
        cur_.swap(next_); std::swap(usedCur_, usedNext_); out_.swap(outN_); pend_.swap(pendN_); std::swap(jobC_, jobN_);
        jobN_.state = Job::Idle; fadeLeft_ = 0;
    }
    void boundary() {
        finishJob(jobC_, cur_, usedCur_, pend_);
        if (fadeLeft_ > 0) finishJob(jobN_, next_, usedNext_, pendN_);
        out_.swap(pend_);
        if (fadeLeft_ > 0) outN_.swap(pendN_);
        head_ = (head_ + 1) % P_;
        rfft_.forward(in_.data(), fdl_[static_cast<size_t>(head_)].data());
        std::copy(in_.begin() + B_, in_.end(), in_.begin());
        startJob(jobC_, head_);
        if (fadeLeft_ > 0) startJob(jobN_, head_);
        pos_ = 0;
    }
    int B_ = 1024, P_ = 1, head_ = 0, pos_ = 0, fadeLen_ = 960, fadeLeft_ = 0, partNext_ = 0, usedStage_ = 1, usedCur_ = 1, usedNext_ = 1;
    RealFft rfft_;
    std::vector<std::vector<cd>> cur_, next_, stage_, fdl_;
    std::vector<double> in_, out_, pend_, outN_, pendN_, pendK_;
    std::vector<double> wr_;
    std::vector<cd> scratch_;
    Job jobC_, jobN_;
};

}  // namespace sw
