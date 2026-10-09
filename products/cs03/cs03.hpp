// SW CS03 Stepped Strip — transformer preamp + EQ06-style stepped EQ + auto-timed compressor
// (spec: 仕様書 v1.0「CS03 Stepped Strip」). Gain scale 0..60 = -30..+30 dB (scale 30 = 0 dB, spec proposal).
// Pre: transformer saturation (lows saturate first), 2x OS; Hi Z = instrument load (top-end shelf) + even harmonics.
// EQ: 100 Hz shelf / 1.5 kHz proportional-Q bell / 10 kHz shelf, 2 dB steps, 30 ms Glide (EQ06).
// Comp: feed-forward, program-dependent attack (3..30 ms) and two-stage auto release (100 ms / 1.2 s).
// EVO input level matching (listen 5 s, write Gain) comes with the UI.
#pragma once
#include "sw/dynamics.hpp"
#include "sw/oversample.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include "sw/unit.hpp"
#include <array>
#include <vector>

namespace sw::cs03 {

enum ParamId { Gain, Impedance, Low, Mid, High, Thresh, Ratio, Knee, Output, Oversample, Unit, kNumParams };

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
    void updateEq(int ramp);
    void updateSplit();
    int unit_ = 0;                          // Unit A / B / C (sw/unit.hpp): slots 0 low, 1 mid, 2 high, and the transformer
    std::array<double, 2> onset_{1.0, 1.0};   // where the transformer saturates, per channel (the signal goes in times k and comes out divided by k: the small-signal gain stays)
    double fs_ = 48000.0;
    std::array<double, kNumParams> target_{};
    LinearSmoother gain_, hiZ_, low_, mid_, high_;
    struct Ch { OsSwitch os; OnePole split, dc; Svf load, low, mid, high; LevelDetector det; };
    std::array<Ch, 2> ch_{};
    GainComputer gc_;
    Ballistics fast_, slow_;
};

}  // namespace sw::cs03
