// SW AUDIO core — wow / flutter: an interpolated (4-point Hermite) variable delay around a fixed centre (SA01 Tape, SA07 Lo-Fi, MD products).
// advance() once per sample moves the slow irregular modulation; read(ch, x) writes the sample and returns the delayed one.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sw {

class WobbleDelay {
public:
    void prepare(double fs, int centreSamples) {
        fs_ = fs; centre_ = std::max(2, centreSamples);
        for (auto& r : ring_) r.assign(kRing, 0.0f);
        pos_ = 0; wow_[0] = wow_[1] = 0; fl_[0] = fl_[1] = fl_[2] = 0; drift_ = driftTarget_ = 0; count_ = 0; rng_ = 0x2468ace1u;
    }
    // wowSamples / flutterSamples: peak swing of the delay in samples; returns the current modulation (samples)
    double advance(double wowSamples, double flutterSamples) {
        constexpr double kPi = 3.14159265358979323846;
        if ((++count_ & 255) == 0) driftTarget_ = ((rng_ = rng_ * 1664525u + 1013904223u) / 4294967296.0 - 0.5);
        drift_ += (driftTarget_ - drift_) * (1.0 / (0.35 * fs_));
        wow_[0] += 2.0 * kPi * 0.62 / fs_; wow_[1] += 2.0 * kPi * (1.37 + 0.4 * drift_) / fs_;
        fl_[0] += 2.0 * kPi * 6.1 / fs_; fl_[1] += 2.0 * kPi * (9.7 + 2.0 * drift_) / fs_; fl_[2] += 2.0 * kPi * 14.3 / fs_;
        mod_ = wowSamples * (0.7 * std::sin(wow_[0]) + 0.3 * std::sin(wow_[1])) + flutterSamples * (0.5 * std::sin(fl_[0]) + 0.3 * std::sin(fl_[1]) + 0.2 * std::sin(fl_[2]));
        return mod_;
    }
    double read(int ch, double x) {
        auto& r = ring_[static_cast<size_t>(ch)];
        r[static_cast<size_t>(pos_)] = static_cast<float>(x);
        return hermite(r, static_cast<double>(pos_) - centre_ - mod_);
    }
    void step() { pos_ = (pos_ + 1) & (kRing - 1); }   // after both channels have been read
    int centre() const { return centre_; }

private:
    static constexpr int kRing = 4096;
    static double hermite(const std::vector<float>& r, double pos) {
        const long i = static_cast<long>(std::floor(pos)); const double f = pos - static_cast<double>(i);
        auto at = [&](long k) { return static_cast<double>(r[static_cast<size_t>(((k % kRing) + kRing) % kRing)]); };
        const double y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
        const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
    double fs_ = 48000.0, mod_ = 0, drift_ = 0, driftTarget_ = 0;
    int centre_ = 48, pos_ = 0;
    unsigned count_ = 0;
    uint32_t rng_ = 0x2468ace1u;
    double wow_[2] = {0, 0}, fl_[3] = {0, 0, 0};
    std::array<std::vector<float>, 2> ring_;
};

}  // namespace sw
