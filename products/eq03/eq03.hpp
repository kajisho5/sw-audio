// SW EQ03 Mid Shaper — passive midrange dip + peak (spec: 仕様書 v1.0「EQ03 Mid Shaper」)
// Dip / Peak: two TPT SVF bells, Width shared. EVO Ride: 200 Hz-5 kHz RMS (attack 50 ms / release 300 ms);
// the louder above the reference (-18 dBFS RMS, spec proposal), the lower the effective Peak (fully off 12 dB above).
#pragma once
#include "sw/drive.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::eq03 {

enum ParamId { DipFreq, Dip, PeakFreq, Peak, Width, Drive, Output, Ride, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double rideFactor() const { return rideMul_; }

private:
    void update(int ramp);
    double fs_ = 48000.0, ms_ = 0, rideMul_ = 1, att_ = 0, rel_ = 0;
    std::array<double, kNumParams> target_{};
    LinearSmoother dipF_, dipG_, peakF_, peakG_, width_;
    std::array<Svf, 2> dip_{}, peak_{};
    Svf bandHp_, bandLp_;
    DriveStage drive_;
};

}  // namespace sw::eq03
