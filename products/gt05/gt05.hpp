// SW GT05 Reamp — what the amp's input sees: pickup, cable and load (spec: 仕様書 v1.0「GT05 Reamp」)
//   in -> Level -> [Pickup swap: the DI's own resonance estimated from its spectrum and cut] -> the pickup/cable/load low-pass -> Output.
//   The low-pass is the real circuit's transfer function: a pickup (inductance L, series resistance R, self capacitance Cp) into the cable capacitance Cc
//   and the load Rl:  H(s) = 1 / (L C s^2 + (L / Rl + R C) s + 1 + R / Rl),  C = Cp + Cc.  Pickup 0..100 % goes from a single coil (L 2.5 H, R 6 k, Cp 100 pF)
//   to a humbucker (L 5.5 H, R 9 k, Cp 180 pF). The peak is at about 1 / (2 pi sqrt(L C)) (2..7 kHz) and its height depends on Rl.
#pragma once
#include "sw/bandlevels.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::gt05 {

enum ParamId { Level, Impedance, Cable, Pickup, Output, PickupSwap, kNumParams };

const std::vector<ParamSpec>& specs();

// the circuit's response in dB (impedance in ohms, cable in pF, pickup 0..100)
double pickupResponseDb(double f, double impedance, double cablePf, double pickupPct);

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double estimatedHz() const { return estHz_; }          // Pickup swap: the DI's resonance found so far (0 = none)
    double estimatedPeakDb() const { return estDb_; }

private:
    void updateCircuit(bool ramp);
    void estimate();
    double fs_ = 48000.0, estHz_ = 0.0, estDb_ = 0.0, circuitGain_ = 1.0;
    int sinceEstimate_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<Svf, 2> res_{}, cancel_{};
    LinearSmoother level_, output_, circuit_, cancelGain_;
    ThirdOctaveAnalyzer analyzer_;
};

}  // namespace sw::gt05
