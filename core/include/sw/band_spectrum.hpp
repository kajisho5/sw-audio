// SW AUDIO core — the long-term average spectrum of a signal in 1/6-octave bands (EQ05 Match: the reference's and the input's tone curve are compared on this): frames of ~85 ms (Hann, hop half a frame, per sample,
// so the result does not depend on how the host cuts the audio); a frame quieter than -70 dBFS is not playing and does not count; the level of a band is the mean power per FFT bin in it (dB), and a band
// narrower than a bin takes the level between its neighbouring bins. 60 bands from 20 Hz (20 x 2^(b/6): the last ends at 20.5 kHz). Allocates nothing after prepare().
// With setLimitSeconds() it stops taking samples once it has that much playing (full()): it ends on the same frame whatever the size of the blocks it was given.
#pragma once
#include "sw/fft.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace sw {

class BandSpectrum {
public:
    static constexpr int kBands = 60;
    static constexpr double kF0 = 20.0, kQuietDb = -70.0;
    static double lowHz(int b) { return kF0 * std::pow(2.0, b / 6.0); }
    static double centerHz(int b) { return kF0 * std::pow(2.0, (b + 0.5) / 6.0); }

    void prepare(double fs) {
        fs_ = fs;
        n_ = 1; while (n_ < static_cast<int>(0.085 * fs)) n_ <<= 1;
        hop_ = n_ / 2;
        rfft_.setup(n_);
        win_.resize(static_cast<size_t>(n_)); for (int i = 0; i < n_; ++i) win_[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * (i + 0.5) / n_);
        buf_.assign(static_cast<size_t>(n_), 0.0); tmp_.assign(static_cast<size_t>(n_), 0.0); spec_.assign(static_cast<size_t>(n_ / 2 + 1), std::complex<double>(0, 0));
        acc_.assign(static_cast<size_t>(n_ / 2 + 1), 0.0);
        limit_ = 0;
        start();
    }
    void start() { pos_ = 0; playing_ = 0; std::fill(acc_.begin(), acc_.end(), 0.0); }
    double playingSeconds() const { return static_cast<double>(playing_) * hop_ / fs_; }
    void setLimitSeconds(double s) { limit_ = s > 0.0 ? std::max<long>(1, static_cast<long>(std::ceil(s * fs_ / hop_))) : 0; }   // 0 = no limit
    bool full() const { return limit_ > 0 && playing_ >= limit_; }

    void process(const float* x, int n) {
        for (int i = 0; i < n; ++i) {
            if (full()) return;
            buf_[static_cast<size_t>(pos_++)] = x[i];
            if (pos_ == n_) { frame(); std::copy(buf_.begin() + hop_, buf_.end(), buf_.begin()); pos_ = hop_; }
        }
    }

    // The frequencies (at most kMaxPoints) with weights (summing to 1) at which a smooth power response is to be sampled to predict what levels() reads of band b for a flat spectrum: the mean over the bins in the
    // band (a few representative bins when there are many), or, when the band is narrower than a bin, the two neighbouring bins it is interpolated from. Where single bins are read (few per band: the lows), the
    // Hann window's own smoothing is in it (a bin reads 2/3 of its own power and 1/6 of each neighbour's: on a steep slope the reading is higher than the response at the bin).
    static constexpr int kPoints = 4, kMaxPoints = 12;
    int samplePoints(int b, double* freq, double* weight) const {
        const int bins = n_ / 2 + 1; const double binHz = fs_ / n_;
        const double lo = lowHz(b), hi = lowHz(b + 1);
        const int k0 = std::max(1, static_cast<int>(std::ceil(lo / binHz)));
        int k1 = k0; while (k1 < bins && k1 * binHz < hi) ++k1;   // bins k0 .. k1-1
        const int c = k1 - k0;
        int n = 0;
        auto addBin = [&](double k, double w) { static const double kern[3] = {1.0 / 6.0, 2.0 / 3.0, 1.0 / 6.0}; for (int j = -1; j <= 1; ++j) { freq[n] = std::abs(k + j) * binHz; weight[n] = w * kern[j + 1]; ++n; } };
        if (c > kPoints) {
            for (int i = 0; i < kPoints; ++i) { const int a = k0 + c * i / kPoints, e = k0 + c * (i + 1) / kPoints; freq[n] = 0.5 * (a + e - 1) * binHz; weight[n] = static_cast<double>(e - a) / c; ++n; }
        } else if (c > 0) {
            for (int k = k0; k < k1; ++k) addBin(k, 1.0 / c);
        } else {
            const double x = centerHz(b) / binHz; const int k = std::clamp(static_cast<int>(x), 0, bins - 2); const double t = std::clamp(x - k, 0.0, 1.0);
            addBin(k, 1.0 - t); addBin(k + 1, t);
        }
        return n;
    }

    // the mean level of each band in dB (power per bin); false when no playing was heard
    bool levels(double* db) const {
        if (playing_ == 0) return false;
        const int bins = n_ / 2 + 1; const double binHz = fs_ / n_;
        auto psd = [&](double f) {   // the average power per bin at f, between the neighbouring bins
            const double x = f / binHz; const int k = std::clamp(static_cast<int>(x), 0, bins - 2); const double t = std::clamp(x - k, 0.0, 1.0);
            return (acc_[static_cast<size_t>(k)] * (1.0 - t) + acc_[static_cast<size_t>(k + 1)] * t) / static_cast<double>(playing_);
        };
        for (int b = 0; b < kBands; ++b) {
            const double lo = lowHz(b), hi = lowHz(b + 1); double s = 0; int c = 0;
            for (int k = std::max(1, static_cast<int>(std::ceil(lo / binHz))); k < bins && k * binHz < hi; ++k) { s += acc_[static_cast<size_t>(k)]; ++c; }
            const double p = c > 0 ? s / (static_cast<double>(c) * static_cast<double>(playing_)) : psd(centerHz(b));
            db[b] = 10.0 * std::log10(std::max(p, 1e-30));
        }
        return true;
    }

private:
    void frame() {
        double ms = 0; for (int i = 0; i < n_; ++i) ms += buf_[static_cast<size_t>(i)] * buf_[static_cast<size_t>(i)];
        if (ms / n_ < std::pow(10.0, kQuietDb / 10.0)) return;
        for (int i = 0; i < n_; ++i) tmp_[static_cast<size_t>(i)] = buf_[static_cast<size_t>(i)] * win_[static_cast<size_t>(i)];
        rfft_.forward(tmp_.data(), spec_.data());
        for (size_t k = 0; k < acc_.size(); ++k) acc_[k] += std::norm(spec_[k]);
        ++playing_;
    }
    double fs_ = 48000.0;
    int n_ = 4096, hop_ = 2048, pos_ = 0;
    long playing_ = 0, limit_ = 0;
    RealFft rfft_;
    std::vector<double> win_, buf_, tmp_, acc_;
    std::vector<std::complex<double>> spec_;
};

}  // namespace sw
