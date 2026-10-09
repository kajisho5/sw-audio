// SW AUDIO core — "learn the bleed" (DY04 Gate, CS02 Console Strip; spec: 進化機能 区分 B): a drum mic hears its own drum and the others. While it listens, every onset of the key signal
// is measured (the peak level in the next ~21 ms, and the spectral centroid of those samples); finish() splits the onsets into two groups (2-means on the level and the centroid), the louder
// group is the wanted drum and the other is bleed, and gives
//   - a threshold between the two (halfway between the quietest wanted hit and the loudest bleed; halfway between the means when they overlap),
//   - a high-pass and a low-pass frequency for the key filters, at the edges of the wanted hits' band (and, where the bleed lies above or below it, between the two).
// It does not touch the audio and allocates nothing after prepare(). Per-sample, so the result does not depend on how the host cuts the audio.
#pragma once
#include "sw/fft.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sw {

class BleedLearner {
public:
    struct Result { bool ok = false; double thresholdDb = -80.0, hpfHz = 100.0, lpfHz = 8000.0; int targetCount = 0, bleedCount = 0; };
    static constexpr int kMaxOnsets = 512;

    void prepare(double fs) {
        fs_ = fs;
        n_ = 1; while (n_ < static_cast<int>(0.02 * fs)) n_ <<= 1;
        rfft_.setup(n_);
        win_.resize(static_cast<size_t>(n_)); for (int i = 0; i < n_; ++i) win_[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * (i + 0.5) / n_);
        cap_.assign(static_cast<size_t>(n_), 0.0); spec_.assign(static_cast<size_t>(n_ / 2 + 1), std::complex<double>(0, 0));
        atk_ = 1.0 - std::exp(-1.0 / (0.0003 * fs)); rel_ = std::exp(-1.0 / (0.02 * fs)); slowC_ = 1.0 - std::exp(-1.0 / (0.015 * fs));
        refractory_ = static_cast<int>(0.08 * fs);
        cancel();
    }
    // listen for at most `maxSeconds` (it stops by itself then; finish() still gives the result)
    void start(double maxSeconds) {
        cancel(); started_ = true; total_ = left_ = std::max<long>(1, static_cast<long>(maxSeconds * fs_));
    }
    bool learning() const { return left_ > 0; }
    double progress() const { return started_ && total_ > 0 ? 1.0 - static_cast<double>(left_) / static_cast<double>(total_) : 0.0; }
    int onsets() const { return count_; }
    double onsetPeakDb(int i) const { return peak_[static_cast<size_t>(i)]; }          // what was measured, for a display and for the tests
    double onsetCentroidHz(int i) const { return cent_[static_cast<size_t>(i)]; }

    // the key signal, mono
    void process(const float* key, int n) {
        for (int i = 0; i < n && left_ > 0; ++i, --left_) {
            const double x = key[i], a = std::abs(x);
            env_ = a > env_ ? env_ + atk_ * (a - env_) : env_ * rel_ + a * (1.0 - rel_);
            slow_ += slowC_ * (env_ - slow_);
            ++since_;
            if (capturing_) {
                cap_[static_cast<size_t>(capN_++)] = x;
                if (capN_ == n_) { analyse(); capturing_ = false; since_ = 0; }
            } else if (since_ > refractory_ && env_ > 1.78e-3 && env_ > 2.5 * slow_ && count_ < kMaxOnsets) {   // -55 dBFS and 8 dB over the average of the last ~15 ms
                capturing_ = true; capN_ = 0; cap_[static_cast<size_t>(capN_++)] = x;
            }
        }
    }

    Result finish() {
        Result r;
        if (!started_) return r;
        started_ = false; left_ = 0; capturing_ = false;
        const int n = count_; count_ = 0;   // (the measured values stay in the arrays until the next onset overwrites them)
        if (n < 6) return r;
        // 2-means on (peak level in 6 dB units, centroid in octaves), started at the quietest and the loudest onset
        std::array<double, kMaxOnsets> f1{}, f2{}; std::array<int, kMaxOnsets> grp{};
        int lo = 0, hi = 0;
        for (int i = 0; i < n; ++i) { f1[static_cast<size_t>(i)] = peak_[static_cast<size_t>(i)] / 6.0; f2[static_cast<size_t>(i)] = std::log2(std::max(cent_[static_cast<size_t>(i)], 20.0)); if (peak_[static_cast<size_t>(i)] < peak_[static_cast<size_t>(lo)]) lo = i; if (peak_[static_cast<size_t>(i)] > peak_[static_cast<size_t>(hi)]) hi = i; }
        double c1[2] = {f1[static_cast<size_t>(lo)], f1[static_cast<size_t>(hi)]}, c2[2] = {f2[static_cast<size_t>(lo)], f2[static_cast<size_t>(hi)]};
        int cnt[2] = {0, 0};
        for (int it = 0; it < 40; ++it) {
            double s1[2] = {0, 0}, s2[2] = {0, 0}; cnt[0] = cnt[1] = 0;
            for (int i = 0; i < n; ++i) {
                const double d0 = sq(f1[static_cast<size_t>(i)] - c1[0]) + sq(f2[static_cast<size_t>(i)] - c2[0]), d1 = sq(f1[static_cast<size_t>(i)] - c1[1]) + sq(f2[static_cast<size_t>(i)] - c2[1]);
                const int g = d1 < d0 ? 1 : 0; grp[static_cast<size_t>(i)] = g; s1[g] += f1[static_cast<size_t>(i)]; s2[g] += f2[static_cast<size_t>(i)]; ++cnt[g];
            }
            if (cnt[0] == 0 || cnt[1] == 0) return r;
            for (int g = 0; g < 2; ++g) { c1[g] = s1[g] / cnt[g]; c2[g] = s2[g] / cnt[g]; }
        }
        const int quiet = c1[0] <= c1[1] ? 0 : 1, loud = 1 - quiet;
        if (cnt[0] < 3 || cnt[1] < 3) return r;
        const double meanQ = c1[quiet] * 6.0, meanL = c1[loud] * 6.0;
        if (meanL - meanQ < 4.0) return r;   // the two groups do not differ in level: a threshold cannot tell them apart
        std::array<double, kMaxOnsets> pq, pl, cq, cl; int nq = 0, nl = 0;   // (on the stack: finish() runs on the audio thread, which does not allocate)
        for (int i = 0; i < n; ++i) {
            const size_t k = static_cast<size_t>(i);
            if (grp[k] == quiet) { pq[static_cast<size_t>(nq)] = peak_[k]; cq[static_cast<size_t>(nq)] = cent_[k]; ++nq; } else { pl[static_cast<size_t>(nl)] = peak_[k]; cl[static_cast<size_t>(nl)] = cent_[k]; ++nl; }
        }
        const double loudMin = percentile(pl.data(), nl, 0.10), quietMax = percentile(pq.data(), nq, 0.90);
        double thr = loudMin > quietMax ? 0.5 * (loudMin + quietMax) : 0.5 * (meanL + meanQ);
        thr = std::clamp(thr, -80.0, -1.0);
        const double tLo = percentile(cl.data(), nl, 0.10), tHi = percentile(cl.data(), nl, 0.90), bLo = percentile(cq.data(), nq, 0.10), bHi = percentile(cq.data(), nq, 0.90);
        const double mq = mean(cq.data(), nq), ml = mean(cl.data(), nl);
        double hpf = 0.25 * tLo, lpf = 2.5 * tHi;
        if (mq < ml) hpf = std::clamp(std::sqrt(tLo * bHi), hpf, 0.8 * tLo);          // bleed below the wanted hits: the high-pass between them
        else if (mq > ml) lpf = std::clamp(std::sqrt(tHi * bLo), 1.5 * tHi, lpf);     // bleed above: the low-pass between them
        r.ok = true; r.thresholdDb = thr; r.hpfHz = std::clamp(hpf, 20.0, 2000.0); r.lpfHz = std::clamp(lpf, 1000.0, 20000.0);
        r.targetCount = nl; r.bleedCount = nq;
        return r;
    }

private:
    static double sq(double x) { return x * x; }
    static double mean(const double* v, int n) { double s = 0; for (int i = 0; i < n; ++i) s += v[i]; return n > 0 ? s / n : 0.0; }
    static double percentile(double* v, int n, double p) { if (n <= 0) return 0.0; std::sort(v, v + n); return v[std::min(n - 1, static_cast<int>(p * static_cast<double>(n - 1) + 0.5))]; }   // (sorts in place)
    void cancel() { started_ = false; left_ = total_ = 0; count_ = 0; env_ = slow_ = 0.0; since_ = refractory_ + 1; capturing_ = false; capN_ = 0; }
    // the measure of one onset: the peak of the captured samples, and their spectral centroid (100 Hz up to 0.45 fs)
    void analyse() {
        double pk = 0; for (int i = 0; i < n_; ++i) pk = std::max(pk, std::abs(cap_[static_cast<size_t>(i)]));
        for (int i = 0; i < n_; ++i) cap_[static_cast<size_t>(i)] *= win_[static_cast<size_t>(i)];
        rfft_.forward(cap_.data(), spec_.data());
        double num = 0, den = 0; const double binHz = fs_ / n_;
        for (int k = 1; k <= n_ / 2; ++k) { const double f = k * binHz; if (f < 100.0 || f > 0.45 * fs_) continue; const double m = std::abs(spec_[static_cast<size_t>(k)]); num += f * m; den += m; }
        peak_[static_cast<size_t>(count_)] = 20.0 * std::log10(std::max(pk, 1e-6)); cent_[static_cast<size_t>(count_)] = den > 0.0 ? num / den : 1000.0; ++count_;
    }
    double fs_ = 48000.0, atk_ = 0, rel_ = 0, slowC_ = 0, env_ = 0, slow_ = 0;
    int n_ = 1024, refractory_ = 3840, since_ = 0, capN_ = 0, count_ = 0;
    long left_ = 0, total_ = 0;
    bool started_ = false, capturing_ = false;
    RealFft rfft_;
    std::vector<double> win_, cap_;
    std::vector<std::complex<double>> spec_;
    std::array<double, kMaxOnsets> peak_{}, cent_{};
};

}  // namespace sw
