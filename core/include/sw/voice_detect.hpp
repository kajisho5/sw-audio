// SW AUDIO core — a rule-based voice detector (voiced speech and singing vs claps, knocks and steady noise), shared by LV05 and LV29.
//   The input is brought to about 16 kHz (box average), cut into 20 ms frames every 10 ms. A frame is "voiced" when
//     - its level is at least 10 dB over the noise floor (minimum of the frame levels; falls at once, rises 3 dB/s, never above -45 dBFS) and over -55 dBFS,
//     - the normalised autocorrelation peak for pitch periods of 70..400 Hz is at least 0.5 (a harmonic, periodic sound),
//     - the zero-crossing rate is under 0.2 per sample (at 16 kHz: dominant energy under about 1.6 kHz; claps and noise are well above).
//   The voice is "active" when 2 of the last 5 frames are voiced. Unvoiced consonants are bridged by the caller's hold time.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace sw {

class VoiceDetector {
public:
    void prepare(double fs) {
        fs_ = fs; dec_ = std::max(1, static_cast<int>(std::lround(fs / 16000.0))); fd_ = fs / dec_;
        frame_ = static_cast<int>(std::lround(0.02 * fd_)); hop_ = frame_ / 2;
        buf_.assign(static_cast<size_t>(frame_), 0.0); tmp_.assign(static_cast<size_t>(frame_), 0.0); pos_ = 0; sinceHop_ = 0; acc_ = 0; accN_ = 0;
        floor_ = 1e-4; hist_ = 0; active_ = false; levelDb_ = -120; lastR_ = 0; lastZ_ = 0; started_ = 0;
    }
    // feeds mono samples; returns the activity after the last sample
    bool process(const float* x, int n) {
        for (int i = 0; i < n; ++i) {
            acc_ += x[i]; if (++accN_ < dec_) continue;
            push(acc_ / dec_); acc_ = 0; accN_ = 0;
        }
        return active_;
    }
    bool active() const { return active_; }
    double levelDb() const { return levelDb_; }
    double floorDb() const { return 20.0 * std::log10(std::max(floor_, 1e-9)); }
    double periodicity() const { return lastR_; }
    double zeroCrossing() const { return lastZ_; }

private:
    void push(double v) {
        buf_[static_cast<size_t>(pos_)] = v; pos_ = (pos_ + 1) % frame_;
        if (++started_ < frame_) return;
        if (++sinceHop_ >= hop_) { sinceHop_ = 0; frameDone(); }
    }
    void frameDone() {
        const int N = frame_; std::vector<double>& f = tmp_;   // (sized in prepare(): this runs on the audio thread)
        double mean = 0; for (int i = 0; i < N; ++i) { f[static_cast<size_t>(i)] = buf_[static_cast<size_t>((pos_ + i) % N)]; mean += f[static_cast<size_t>(i)]; }
        mean /= N; double e = 0; for (auto& v : f) { v -= mean; e += v * v; }
        const double rms = std::sqrt(e / N); levelDb_ = 20.0 * std::log10(std::max(rms, 1e-9));
        // noise floor: down at once, up 3 dB/s (frames are 10 ms apart)
        if (rms < floor_) floor_ = std::max(1e-6, rms); else floor_ = std::min(std::max(floor_ * std::pow(10.0, 3.0 / 20.0 * 0.01), 1e-6), std::max(floor_, std::pow(10.0, -45.0 / 20.0)));
        int zc = 0; for (int i = 1; i < N; ++i) if ((f[static_cast<size_t>(i)] >= 0) != (f[static_cast<size_t>(i - 1)] >= 0)) ++zc;
        lastZ_ = static_cast<double>(zc) / N;
        const int lo = std::max(2, static_cast<int>(fd_ / 400.0)), hi = std::min(N - 2, static_cast<int>(fd_ / 70.0));
        double best = 0;
        if (e > 1e-12) for (int L = lo; L <= hi; ++L) {
            double c = 0, e2 = 0; for (int i = 0; i + L < N; ++i) { c += f[static_cast<size_t>(i)] * f[static_cast<size_t>(i + L)]; e2 += f[static_cast<size_t>(i + L)] * f[static_cast<size_t>(i + L)]; }
            double e1 = 0; for (int i = 0; i + L < N; ++i) e1 += f[static_cast<size_t>(i)] * f[static_cast<size_t>(i)];
            const double d = std::sqrt(e1 * e2); if (d > 1e-18) best = std::max(best, c / d);
        }
        lastR_ = best;
        const bool voiced = rms > 1.78e-3 && rms > floor_ * 3.1623 && best >= 0.5 && lastZ_ < 0.2;   // -55 dBFS, floor + 10 dB
        hist_ = ((hist_ << 1) | (voiced ? 1 : 0)) & 31;
        int cnt = 0; for (int k = 0; k < 5; ++k) cnt += (hist_ >> k) & 1;
        active_ = cnt >= 2;
    }
    double fs_ = 48000, fd_ = 16000, acc_ = 0, floor_ = 1e-4, levelDb_ = -120, lastR_ = 0, lastZ_ = 0;
    int dec_ = 3, frame_ = 320, hop_ = 160, pos_ = 0, sinceHop_ = 0, accN_ = 0, started_ = 0, hist_ = 0;
    bool active_ = false;
    std::vector<double> buf_, tmp_;
};

}  // namespace sw
