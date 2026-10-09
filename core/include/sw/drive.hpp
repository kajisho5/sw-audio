// SW AUDIO core — analog output stage (EQ01 / EQ03 / EQ04 family):
// Drive 0..10 = input 0..+18 dB into ONE asymmetric soft-clip stage at 2x oversampling (1x / 2x / 4x: setOversample), with level compensation.
// (spec: 「Drive：偶数次寄りの非対称ソフトクリップ1段、2× OS。ドライブ量に応じて出力を自動で下げ、Drive を回しても音量がほぼ変わらない」)
//
//   v = g x / h + b                      g = Drive gain (1..7.94), h = headroom (2.0 = +6 dBFS, decision), b = 0.3 * Drive / 10 (design value)
//   y = h s (tanh(v) - tanh(b)) / g      s = 1 / sech^2(b): slope 1 at x = 0, so small signals pass at unity for any Drive
//
// The bias moves the operating point off the centre of the tanh, so the two half-waves clip differently:
// the 2nd harmonic leads the 3rd (b = 0.3 at Drive 10 gives 2nd -21 dB / 3rd -30 dB at Drive 10 with a 0.18 sine; measured in tests/test_drive.cpp).
// Level compensation: the 1/g makes the stage's small-signal gain constant and takes the clipped peaks down by the same amount
// the input was raised, so a -18 dBFS RMS tone changes by 1.4 dB at most (Drive 10) across Drive 0..10.
// Drive 0 has no bias: the stage is a plain tanh with +6 dBFS headroom and adds no even harmonics.
// The asymmetry shifts the mean of the output; the shift (output minus input) is high-passed at 5 Hz inside the 2x loop.
// Only the added distortion is filtered, so the linear path and Drive 0 at normal levels are untouched.
#pragma once
#include "sw/oversample.hpp"
#include "sw/smooth.hpp"
#include <array>
#include <cmath>

namespace sw {

class DriveStage {
public:
    static constexpr double kHeadroom = 2.0;  // +6 dBFS (decision)
    static constexpr double kBias = 0.3;      // operating-point offset at Drive 10, proportional to Drive (design value; README "Drive 段の設計")
    static constexpr double kDcHz = 5.0;      // DC removal of the distortion product

    void prepare(double fs, double drive010) {
        fs_ = fs; drive_.reset(fs, 20.0, drive010 * 1.8); for (auto& o : os_) o.reset(); dc_ = {};
        setOversample(os_[0].factor());
        update(drive_.current());
    }
    // oversampling 1x / 2x / 4x (spec: common function, default 2x): the DC blocker lives inside the oversampled loop, so its coefficient follows
    void setOversample(int factor) {
        for (auto& o : os_) o.setFactor(factor);
        if (fs_ > 0.0) dcA_ = std::exp(-2.0 * 3.14159265358979323846 * kDcHz / (os_[0].factor() * fs_));
    }
    int oversample() const { return os_[0].factor(); }
    void set(double drive010) { drive_.setTarget(drive010 * 1.8); }
    void snap() { drive_.skip(1 << 30); update(drive_.current()); }
    void tick() { if (drive_.isSmoothing()) update(drive_.next()); }  // once per sample, before process()
    double process(int ch, double x) {
        const size_t c = static_cast<size_t>(ch);
        return os_[c].process(x, [&](double u) { return shape(c, u); });
    }

private:
    void update(double driveDb) {
        g_ = std::pow(10.0, driveDb / 20.0);
        b_ = kBias * driveDb / 18.0;
        tb_ = std::tanh(b_); s_ = 1.0 / (1.0 - tb_ * tb_);
    }
    double shape(size_t ch, double u) {
        const double y = kHeadroom * s_ * (std::tanh(g_ * u / kHeadroom + b_) - tb_) / g_;
        const double d = y - u;                           // added distortion, incl. the DC shift
        dc_[ch] = dcA_ * dc_[ch] + (1.0 - dcA_) * d;      // its mean
        return u + (d - dc_[ch]);
    }
    LinearSmoother drive_;
    std::array<OsSwitch, 2> os_{};
    std::array<double, 2> dc_{};
    double fs_ = 0.0, g_ = 1.0, b_ = 0.0, dcA_ = 0.0, tb_ = 0.0, s_ = 1.0;
};

}  // namespace sw
