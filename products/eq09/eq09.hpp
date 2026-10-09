// SW EQ09 Tilt — tilt tone shaper (spec: 仕様書 v1.0「EQ09 Tilt」)
// Tilt: opposite low/high shelves at the pivot (half gain each at the pivot -> pivot stays at 0 dB).
// EVO Auto pivot: the pivot follows the spectral centre (RMS frequency, 10 s window, 5 s glide, 300 Hz-4 kHz).
#pragma once
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include "sw/unit.hpp"
#include <array>
#include <vector>

namespace sw::eq09 {

enum ParamId { Tilt, PivotHz, LowLift, Air, Output, AutoPivot, Unit, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double pivotHz() const { return pivot_; }

private:
    void update(int ramp);
    int unit_ = 0;             // Unit A / B / C (sw/unit.hpp): slots 0 the tilt pivot, 1 the low shelf, 2 the air shelf
    double fs_ = 48000.0, pivot_ = 1000.0, ex_ = 0, ed_ = 0, prev_[2] = {0, 0}, avgCoef_ = 0, glideCoef_ = 0;
    std::array<double, kNumParams> target_{};
    LinearSmoother tilt_, low_, air_;
    struct Ch { Svf tiltLo, tiltHi, low, air; };
    std::array<Ch, 2> ch_{};
    int sinceUpdate_ = 0;
};

}  // namespace sw::eq09
