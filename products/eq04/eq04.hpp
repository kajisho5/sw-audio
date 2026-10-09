// SW EQ04 Inductor — inductor console EQ (spec: 仕様書 v1.0「EQ04 Inductor」)
// HPF 18 dB/oct; Low shelf with the inductor bump (Q 1.0: about +1 dB at full boost, design value); Mid bell Q 0.9;
// High shelf. EVO Iron: only the boosted part of each band is saturated (low band most), cut bands stay clean. 2x OS.
#pragma once
#include "sw/drive.hpp"
#include "sw/oversample.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::eq04 {

enum ParamId { Hpf, LowFreq, LowGain, MidFreq, MidGain, HighFreq, HighGain, Drive, Output, Iron, Oversample, kNumParams };

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
    double fs_ = 48000.0;
    std::array<double, kNumParams> target_{};
    LinearSmoother hpfF_, hpfOn_, lowF_, lowG_, midF_, midG_, highF_, highG_, iron_;
    struct Ch { OnePole hp1; Svf hp2, low, mid, high; OsSwitch osLow, osMid, osHigh; };
    std::array<Ch, 2> ch_{};
    DriveStage drive_;
};

}  // namespace sw::eq04
