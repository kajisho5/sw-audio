// SW AUDIO core — "Auto" crossovers of a multiband (DY10; spec: 進化機能 区分 B): it listens to at least 10 s of playing, takes the long-term average power spectrum, weights it by the ear's sensitivity
// (the K-weighting of the loudness meters), cuts the weighted energy into four about equal parts (the quartiles) and moves each cut to the nearest valley of the spectrum (within half an octave), keeping the three an octave apart
// and inside 20 Hz .. 20 kHz. Frames of ~85 ms with a hop of half a frame, taken per sample, so the result does not depend on how the host cuts the audio; frames quieter than -70 dBFS are not playing
// and do not count. Allocates nothing after prepare().
#pragma once
#include "sw/fft.hpp"
#include "sw/loudness.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace sw {

class CrossoverFinder {
public:
    struct Result { bool ok = false; double hz[3] = {240.0, 2000.0, 8000.0}; };
    static constexpr double kPlayingSeconds = 10.0;   // spec: at least 10 s of playing
    static constexpr double kGiveUpSeconds = 90.0;    // listening for longer than this without enough playing: it stops
    static constexpr double kQuietDb = -70.0;         // a frame below this is not playing
    static constexpr double kMinHz = 20.0, kMaxHz = 20000.0;
    static constexpr double kValleyDb = 3.0;          // a cut moves only to a band at least this much lower than the one it is in

    void prepare(double fs) {
        fs_ = fs; k_.setup(fs);
        n_ = 1; while (n_ < static_cast<int>(0.085 * fs)) n_ <<= 1;
        hop_ = n_ / 2;
        rfft_.setup(n_);
        win_.resize(static_cast<size_t>(n_)); for (int i = 0; i < n_; ++i) win_[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * (i + 0.5) / n_);
        buf_.assign(static_cast<size_t>(n_), 0.0); tmp_.assign(static_cast<size_t>(n_), 0.0); spec_.assign(static_cast<size_t>(n_ / 2 + 1), std::complex<double>(0, 0));
        acc_.assign(static_cast<size_t>(n_ / 2 + 1), 0.0);
        const double top = std::min(kMaxHz, 0.45 * fs);   // the scratch of analyse(), made here: it runs on the audio thread, which does not allocate
        cum_.assign(static_cast<size_t>(n_ / 2 + 1), 0.0); nb_ = std::max(1, static_cast<int>(std::ceil(6.0 * std::log2(top / kMinHz))));
        db_.assign(static_cast<size_t>(nb_), 0.0); sm_.assign(static_cast<size_t>(nb_), 0.0);
        cancel();
    }
    void start() { cancel(); listening_ = true; }
    void cancel() { listening_ = false; done_ = false; pos_ = 0; heard_ = 0; playing_ = 0; std::fill(acc_.begin(), acc_.end(), 0.0); result_ = Result{}; }
    bool listening() const { return listening_; }
    bool done() const { return done_; }
    double progress() const { return listening_ || done_ ? std::min(1.0, static_cast<double>(playing_) * hop_ / fs_ / kPlayingSeconds) : 0.0; }   // the seconds of playing heard over the 10 s
    const Result& result() const { return result_; }

    void process(const float* x, int n) {
        for (int i = 0; i < n && listening_; ++i) {
            buf_[static_cast<size_t>(pos_++)] = x[i];
            if (pos_ == n_) {
                frame();
                std::copy(buf_.begin() + hop_, buf_.end(), buf_.begin()); pos_ = hop_;   // the overlap: the second half is the first of the next
                if (playing_ * hop_ >= static_cast<long>(kPlayingSeconds * fs_)) { result_ = analyse(); result_.ok = true; done_ = true; listening_ = false; }
                else if (++heard_ * hop_ >= static_cast<long>(kGiveUpSeconds * fs_)) listening_ = false;
            }
        }
    }

private:
    // the power spectrum of one frame goes into the sum when the frame is playing
    void frame() {
        double ms = 0; for (int i = 0; i < n_; ++i) ms += buf_[static_cast<size_t>(i)] * buf_[static_cast<size_t>(i)];
        if (ms / n_ < std::pow(10.0, kQuietDb / 10.0)) return;
        for (int i = 0; i < n_; ++i) tmp_[static_cast<size_t>(i)] = buf_[static_cast<size_t>(i)] * win_[static_cast<size_t>(i)];
        rfft_.forward(tmp_.data(), spec_.data());
        for (size_t k = 0; k < acc_.size(); ++k) acc_[k] += std::norm(spec_[k]);
        ++playing_;
    }
    // the ear's sensitivity: the K-weighting of BS.1770 (a head-effect shelf and the 38 Hz high-pass: the loudness meters' own) as a power ratio at f
    static double biquadPower(const Biquad& q, double w) {
        const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2.0 * w);
        return std::norm((q.b0 + q.b1 * z1 + q.b2 * z2) / (1.0 + q.a1 * z1 + q.a2 * z2));
    }
    double weightPower(double f) const { const double w = 2.0 * 3.14159265358979323846 * f / fs_; return biquadPower(k_.shelf(), w) * biquadPower(k_.highpass(), w); }
    Result analyse() {
        Result r; const int bins = n_ / 2 + 1; const double binHz = fs_ / n_;
        const double top = std::min(kMaxHz, 0.45 * fs_);
        // the cumulative weighted energy over the bins in 20 Hz .. top
        std::vector<double>& cum = cum_; std::fill(cum.begin(), cum.end(), 0.0); double total = 0.0;
        for (int k = 1; k < bins; ++k) { const double f = k * binHz; if (f >= kMinHz && f <= top) total += acc_[static_cast<size_t>(k)] * weightPower(f); cum[static_cast<size_t>(k)] = total; }
        if (!(total > 0.0)) return r;
        double q[3];
        for (int j = 0; j < 3; ++j) {   // the frequency where the cumulative energy reaches 25, 50, 75 %
            const double target = total * 0.25 * (j + 1); int k = 1; while (k < bins - 1 && cum[static_cast<size_t>(k)] < target) ++k;
            const double c0 = cum[static_cast<size_t>(k - 1)], c1 = cum[static_cast<size_t>(k)], frac = c1 > c0 ? (target - c0) / (c1 - c0) : 0.0;
            q[j] = std::clamp((k - 1 + frac) * binHz, kMinHz, top);
        }
        // the valleys: the mean power per bin in 1/6-octave bands (20 Hz up), in dB, smoothed over three bands
        const int nb = nb_; std::vector<double>& db = db_; std::fill(db.begin(), db.end(), -300.0);
        for (int b = 0; b < nb; ++b) {
            const double lo = kMinHz * std::pow(2.0, b / 6.0), hi = std::min(top, kMinHz * std::pow(2.0, (b + 1) / 6.0));
            double s = 0; int c = 0; for (int k = std::max(1, static_cast<int>(std::ceil(lo / binHz))); k < bins && k * binHz < hi; ++k) { s += acc_[static_cast<size_t>(k)]; ++c; }
            if (c > 0) db[static_cast<size_t>(b)] = 10.0 * std::log10(std::max(s / c, 1e-30));
            else if (b > 0) db[static_cast<size_t>(b)] = db[static_cast<size_t>(b - 1)];   // a band narrower than a bin takes its neighbour's level
        }
        std::vector<double>& sm = sm_;
        for (int b = 0; b < nb; ++b) { double s = 0; int c = 0; for (int d = -1; d <= 1; ++d) if (b + d >= 0 && b + d < nb) { s += db[static_cast<size_t>(b + d)]; ++c; } sm[static_cast<size_t>(b)] = s / c; }
        auto bandOf = [&](double f) { return std::clamp(static_cast<int>(std::floor(6.0 * std::log2(f / kMinHz))), 0, nb - 1); };
        for (int j = 0; j < 3; ++j) {   // each cut to the lowest band of the smoothed spectrum within half an octave of where the quartile is, when that band is at least 3 dB lower than the one it is in
            const int b0 = bandOf(q[j]); int best = b0;
            for (int b = std::max(0, b0 - 3); b <= std::min(nb - 1, b0 + 3); ++b) if (sm[static_cast<size_t>(b)] < sm[static_cast<size_t>(best)] - 1e-9) best = b;
            q[j] = sm[static_cast<size_t>(best)] <= sm[static_cast<size_t>(b0)] - kValleyDb ? std::min(top, kMinHz * std::pow(2.0, (best + 0.5) / 6.0)) : q[j];   // (a valley, not the ripple of a flat spectrum)
        }
        // an octave apart, in range: pushed up from the lowest, then, where the top would pass the limit, pushed down from the highest
        std::sort(q, q + 3);
        for (int j = 1; j < 3; ++j) q[j] = std::max(q[j], 2.0 * q[j - 1]);
        if (q[2] > top) { q[2] = top; for (int j = 1; j >= 0; --j) q[j] = std::min(q[j], 0.5 * q[j + 1]); }
        for (int j = 0; j < 3; ++j) r.hz[j] = std::max(q[j], kMinHz);
        r.ok = true; return r;
    }
    double fs_ = 48000.0;
    int n_ = 4096, hop_ = 2048, pos_ = 0;
    long heard_ = 0, playing_ = 0;
    bool listening_ = false, done_ = false;
    Result result_;
    RealFft rfft_;
    KWeighting k_;
    std::vector<double> win_, buf_, tmp_, acc_, cum_, db_, sm_;
    int nb_ = 1;
    std::vector<std::complex<double>> spec_;
};

}  // namespace sw
