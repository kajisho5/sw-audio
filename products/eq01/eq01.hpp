// SW EQ01 Passive — passive-style low shelf + air bell (spec: 仕様書 v1.0「EQ01 Passive 進化版」)
// EVO Contour: the low shelf's Q (0.707 -> 3.0) makes the classic "boost and cut a little above" shape with one knob.
// Mode MS: the EQ and output stage work on the mid; the side passes untouched (interpretation, see README).
#pragma once
#include "sw/drive.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include "sw/unit.hpp"
#include <array>
#include <vector>

namespace sw::eq01 {

enum ParamId { LowFreq, LowGain, Contour, AirFreq, AirGain, Width, Drive, Output, Mode, Oversample, Unit, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }

private:
    void update(int ramp);
    void applyUnit();          // Unit A / B / C: the saturation onset of each channel
    double fs_ = 48000.0;
    int unit_ = 0;             // Unit A / B / C (sw/unit.hpp): slot 0 the low shelf, 1 the air bell, and the drive
    std::array<double, kNumParams> target_{};
    LinearSmoother lowF_, lowG_, contour_, airF_, airG_, width_, ms_;
    std::array<Svf, 2> low_{}, air_{};
    Svf lowM_, airM_;          // mid path (MS mode) has its own state
    DriveStage drive_, driveM_;
};

}  // namespace sw::eq01
