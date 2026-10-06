// SW AUDIO core — fundamental-frequency tracker (DY05 Pitch follow, MD03 Note follow, MD06 Pitch track ...)
// low-pass 1 kHz, decimate to ~6 kHz, normalised autocorrelation over 64 ms every 10 ms; range 70 .. 1000 Hz.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace sw {

class PitchTracker {
public:
    void prepare(double fs);
    void push(double x);                  // one input sample (mono)
    bool voiced() const { return voiced_; }
    double f0() const { return f0_; }     // last tracked fundamental (Hz), 0 when none yet
    void reset();
private:
    void analyse();
    double fd_ = 6000.0;
    int decim_ = 8, phase_ = 0, hop_ = 0, pos_ = 0;
    double lp1_ = 0, lp2_ = 0, lpC_ = 0, f0_ = 0; int sinceHop_ = 0;
    bool voiced_ = false;
    std::vector<double> ring_;
};

// ---- pitch tracker: low-pass 1 kHz, decimate to ~6 kHz, normalised autocorrelation over 64 ms every 10 ms
inline void PitchTracker::prepare(double fs) {
    decim_ = std::max(1, static_cast<int>(std::lround(fs / 6000.0)));
    fd_ = fs / decim_;
    lpC_ = std::exp(-2.0 * 3.14159265358979323846 * 1000.0 / fs);
    ring_.assign(static_cast<size_t>(std::lround(fd_ * 0.064)), 0.0);
    hop_ = std::max(1, static_cast<int>(std::lround(fd_ * 0.010)));
    reset();
}
inline void PitchTracker::reset() { std::fill(ring_.begin(), ring_.end(), 0.0); lp1_ = lp2_ = 0; phase_ = pos_ = 0; voiced_ = false; f0_ = 0; sinceHop_ = 0; }
inline void PitchTracker::push(double x) {
    lp1_ = x + lpC_ * (lp1_ - x);       // two one-pole low-passes (12 dB/oct)
    lp2_ = lp1_ + lpC_ * (lp2_ - lp1_);
    if (++phase_ < decim_) return;
    phase_ = 0;
    ring_[static_cast<size_t>(pos_)] = lp2_;
    pos_ = (pos_ + 1) % static_cast<int>(ring_.size());
    if (++sinceHop_ >= hop_) { sinceHop_ = 0; analyse(); }
}
inline void PitchTracker::analyse() {
    const int n = static_cast<int>(ring_.size());
    const int minLag = std::max(2, static_cast<int>(fd_ / 1000.0)), maxLag = std::min(n / 2, static_cast<int>(fd_ / 70.0));
    // oldest first
    double e0 = 0;
    auto at = [&](int i) { return ring_[static_cast<size_t>((pos_ + i) % n)]; };
    for (int i = 0; i < n; ++i) e0 += at(i) * at(i);
    if (e0 / n < 1e-9) { voiced_ = false; return; }   // below about -50 dBFS (after the low-pass): nothing to track
    double best = 0; int bestLag = 0;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double r = 0, e1 = 0;
        for (int i = 0; i + lag < n; ++i) { r += at(i) * at(i + lag); e1 += at(i + lag) * at(i + lag); }
        const double nr = r / std::sqrt(e0 * e1 + 1e-30);
        if (nr > best) { best = nr; bestLag = lag; }
    }
    voiced_ = best > 0.6;
    if (voiced_) f0_ = fd_ / bestLag;
}


}  // namespace sw
