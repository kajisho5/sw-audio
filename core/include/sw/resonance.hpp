// SW AUDIO core — resonance finder (EQ02 Assist, spec: "short-time FFT smoothed to 1/24 octave; peaks above the 1/3 octave moving median by a margin, lasting seconds, are resonances").
//   The mono sum goes through a Hann-windowed FFT (0.17 s, hop a quarter of that); the power is averaged into cells of 1/24 octave from 80 Hz to 20 kHz; the excess of a cell is its level in dB minus the
//   median of the nine cells around it (a third of an octave); the excess is averaged over time (time constant 2 s) so only what lasts counts; a mark is a cell whose averaged excess is at least
//   kThresholdDb, that is the largest of its neighbours (+-2 cells), and that is not within a sixth of an octave of a stronger mark. Nothing is reported before 2 s of signal have been seen.
//   Allocates in setup() only; process() is for the audio thread.
#pragma once
#include "sw/fft.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace sw {

class ResonanceFinder {
public:
    static constexpr int kMarks = 6;
    static constexpr double kThresholdDb = 6.0, kMinSeconds = 2.0, kTimeConstant = 2.0;
    struct Mark { double hz = 0, db = 0; };

    void setup(double fs) {
        fs_ = fs; n_ = fs > 64000.0 ? 16384 : 8192; hop_ = n_ / 4;
        fft_.setup(n_); win_.resize(static_cast<size_t>(n_)); for (int i = 0; i < n_; ++i) win_[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n_);
        ring_.assign(static_cast<size_t>(n_), 0.0f); buf_.assign(static_cast<size_t>(n_), {});
        const double df = fs / n_, top = std::min(20000.0, 0.45 * fs);
        lo_.clear(); hi_.clear(); hz_.clear();
        for (int j = 0;; ++j) {
            const double f = 80.0 * std::pow(2.0, j / 24.0); if (f > top) break;
            int a = static_cast<int>(std::ceil(f * std::pow(2.0, -1.0 / 48.0) / df)), b = static_cast<int>(std::floor(f * std::pow(2.0, 1.0 / 48.0) / df));
            if (b < a) a = b = static_cast<int>(std::lround(f / df));
            lo_.push_back(a); hi_.push_back(b); hz_.push_back(f);
        }
        excess_.assign(hz_.size(), 0.0); ema_.assign(hz_.size(), 0.0); db_.assign(hz_.size(), 0.0);
        alpha_ = 1.0 - std::exp(-static_cast<double>(hop_) / (fs * kTimeConstant));
        reset();
    }
    void reset() { std::fill(ring_.begin(), ring_.end(), 0.0f); std::fill(ema_.begin(), ema_.end(), 0.0); pos_ = 0; sinceFrame_ = 0; filled_ = 0; frames_ = 0; }
    // the mono sum of the channels (one or two)
    void process(const float* const* ch, int numCh, int n) {
        if (numCh < 1 || n <= 0 || ring_.empty()) return;
        const int nch = std::min(numCh, 2);
        for (int i = 0; i < n; ++i) {
            const float x = nch > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
            ring_[static_cast<size_t>(pos_)] = std::isfinite(x) ? x : 0.0f; pos_ = (pos_ + 1) % n_;
            if (filled_ < n_) ++filled_;
            if (++sinceFrame_ >= hop_) { sinceFrame_ = 0; if (filled_ >= n_) frame(); }
        }
    }
    // the marks, strongest first; returns how many (<= kMarks)
    int marks(Mark* out) const {
        if (static_cast<double>(frames_) * hop_ / fs_ < kMinSeconds) return 0;
        const int J = static_cast<int>(hz_.size()); int cand[kMaxCand], nc = 0;
        for (int j = 0; j < J && nc < kMaxCand; ++j) {
            if (ema_[static_cast<size_t>(j)] < kThresholdDb) continue;
            bool top = true; for (int d = -2; d <= 2 && top; ++d) { const int k = j + d; if (d != 0 && k >= 0 && k < J && ema_[static_cast<size_t>(k)] > ema_[static_cast<size_t>(j)]) top = false; }
            if (top) cand[nc++] = j;
        }
        std::sort(cand, cand + nc, [&](int a, int b) { return ema_[static_cast<size_t>(a)] > ema_[static_cast<size_t>(b)]; });
        int n = 0;
        for (int c = 0; c < nc && n < kMarks; ++c) {
            bool near = false; for (int k = 0; k < n; ++k) if (std::abs(std::log2(out[k].hz / hz_[static_cast<size_t>(cand[c])])) < 1.0 / 6.0) near = true;
            if (!near) out[n++] = {hz_[static_cast<size_t>(cand[c])], ema_[static_cast<size_t>(cand[c])]};
        }
        return n;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr int kMaxCand = 64;
    void frame() {
        for (int i = 0; i < n_; ++i) buf_[static_cast<size_t>(i)] = ring_[static_cast<size_t>((pos_ + i) % n_)] * win_[static_cast<size_t>(i)];   // oldest first
        fft_.forward(buf_);
        const int J = static_cast<int>(hz_.size());
        for (int j = 0; j < J; ++j) {
            double s = 0; int c = 0; for (int k = lo_[static_cast<size_t>(j)]; k <= hi_[static_cast<size_t>(j)]; ++k) { s += std::norm(buf_[static_cast<size_t>(k)]); ++c; }
            db_[static_cast<size_t>(j)] = 10.0 * std::log10(c > 0 ? s / c + 1e-20 : 1e-20);
        }
        for (int j = 0; j < J; ++j) {
            double w[9]; int m = 0; for (int d = -4; d <= 4; ++d) w[m++] = db_[static_cast<size_t>(std::clamp(j + d, 0, J - 1))];
            std::nth_element(w, w + 4, w + 9);
            excess_[static_cast<size_t>(j)] = db_[static_cast<size_t>(j)] - w[4];
            ema_[static_cast<size_t>(j)] += (excess_[static_cast<size_t>(j)] - ema_[static_cast<size_t>(j)]) * alpha_;
        }
        ++frames_;
    }
    double fs_ = 48000.0, alpha_ = 0.02; int n_ = 8192, hop_ = 2048, pos_ = 0, sinceFrame_ = 0, filled_ = 0; long long frames_ = 0;
    Fft fft_;
    std::vector<double> win_, hz_, excess_, ema_, db_; std::vector<int> lo_, hi_; std::vector<float> ring_; std::vector<std::complex<double>> buf_;
};

}  // namespace sw
