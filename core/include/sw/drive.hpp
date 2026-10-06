// SW AUDIO core — analog output stage (EQ01 / EQ03 / EQ04 / EQ06 family): Drive 0..10 = input 0..+18 dB into a
// tanh stage at 2x oversampling, small-signal unity gain (spec: 「Drive：出力段（EQ01 と同じモジュール）」)
#pragma once
#include "sw/oversample.hpp"
#include "sw/saturate.hpp"
#include "sw/smooth.hpp"
#include <array>

namespace sw {

class DriveStage {
public:
    void prepare(double fs, double drive010) { drive_.reset(fs, 20.0, drive010 * 1.8); os_ = {}; sat_.setHeadroom(2.0); sat_.setDriveDb(drive_.current()); }  // headroom +6 dBFS (design)
    void set(double drive010) { drive_.setTarget(drive010 * 1.8); }
    void snap() { drive_.skip(1 << 30); sat_.setDriveDb(drive_.current()); }
    void tick() { if (drive_.isSmoothing()) sat_.setDriveDb(drive_.next()); }  // once per sample, before process()
    double process(int ch, double x) {
        double up[2];
        os_[static_cast<size_t>(ch)].up(x, up);
        up[0] = sat_.process(up[0]); up[1] = sat_.process(up[1]);
        return os_[static_cast<size_t>(ch)].down(up);
    }

private:
    LinearSmoother drive_;
    std::array<Oversampler2x, 2> os_{};
    Saturator sat_;
};

}  // namespace sw
