// SW SA04 Transformer — transformer and preamp colour (spec: 仕様書 v1.0「SA04 Transformer」)
// Pad (-20 dB) -> Gain (knob 0..60 = -30..+30 dB, 30 = 0 dB, as CS03) -> Low weight / Top air shelves -> transformer equivalent circuit
// (low-frequency loss, high-frequency resonance that Load moves) -> split saturation, lows saturating at one third of the ceiling
// (Iron sets ceiling and bias; 2x OS, sw::BiasShaper2x). EVO Load: the source impedance changes the high-frequency resonance and the amount of lows.
#pragma once
#include "sw/param.hpp"
#include "sw/shaper.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include "sw/unit.hpp"
#include <array>
#include <vector>

namespace sw::sa04 {

enum ParamId { Iron, Gain, Load, LowWeight, TopAir, Output, Pad, Oversample, Unit, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { gain_.skip(1 << 30); }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }

private:
    void updateFilters();
    double fs_ = 48000.0, lpA_ = 0;
    std::array<double, kNumParams> target_{};
    LinearSmoother gain_;
    BiasShaper2x lo_, hi_;
    std::array<double, 2> lp_{};
    int unit_ = 0;   // Unit A / B / C (sw/unit.hpp): slots 0 weight, 1 air, 2 high-pass, 3 loss, 4 resonance, and the two shapers
    void applyUnit();
    struct Ch { Svf weight, air, hp, lfLoss, res; };
    std::array<Ch, 2> f_{};
};

}  // namespace sw::sa04
