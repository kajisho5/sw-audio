// SW DY07 Snap — VCA compressor with program-dependent timing and an attack shaper (spec: 仕様書 v1.0「DY07 Snap」)
// RMS detection, threshold 0 dB = -18 dBFS (spec proposal). Attack speeds up the further over the threshold;
// release recovers at a constant 120 dB/s. EVO Snap: +-6 dB on transient heads, independent of the ratio.
#pragma once
#include "sw/dynamics.hpp"
#include "sw/param.hpp"
#include <array>
#include <vector>

namespace sw::dy07 {

enum ParamId { Threshold, Compress, Output, Snap, Mix, Knee, Unit, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double gainReductionDb() const { return gr_; }
    double recoveryMs() const { return recoveryMs_; }  // last full release (for the meter / tests)

private:
    void updateCurve();
    double fs_ = 48000.0, gr_ = 0, snapGainDb_ = 0, fast_ = 0, slow_ = 0, recoveryMs_ = 0;
    std::array<double, kNumParams> target_{};
    GainComputer comp_;
    std::array<LevelDetector, 2> det_{};
    double fastRel_ = 0, slowCoef_ = 0, snapCoef_ = 0, releaseStep_ = 0;
    bool releasing_ = false;
    long releaseCount_ = 0;
};

}  // namespace sw::dy07
