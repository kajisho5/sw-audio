// SW DY02 Opto — optical leveler with a Ride front fader (spec: 仕様書 v1.0「DY02 Opto 進化版」)
// Cell model: attack ~10 ms; release in two stages (the fast one takes half of the gain reduction, the slow one the rest).
// Speed Fast 40 ms / 0.5 s, Slow 200 ms / 3 s, Prog = model (slow stage stretches with the depth and length of the compression).
// Level 0..10 = threshold 0..-40 dBFS, 3:1 with a 12 dB knee (design). Ride: 400 ms loudness (K-weighted) toward Target, +-12 dB.
#pragma once
#include "sw/dynamics.hpp"
#include "sw/loudness.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::dy02 {

enum ParamId { Level, Output, Speed, Target, Emphasis, Mix, Ride, AutoMakeup, kNumParams };

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
    double rideGainDb() const { return rideDb_; }

private:
    void updateCurve();
    double prevRide_ = 0, fs_ = 48000.0, gr_ = 0, grFast_ = 0, grSlow_ = 0, memory_ = 0, avgGr_ = 0, rideDb_ = 0, rideStep_ = 0, makeupDb_ = 0;
    std::array<double, kNumParams> target_{};
    GainComputer comp_;
    std::array<LevelDetector, 2> det_{};
    std::array<Svf, 2> emph_{};
    LoudnessMeter meter_;
    double attackC_ = 0, fastC_ = 0, slowC_ = 0, memAtt_ = 0, memRel_ = 0, avgC_ = 0, rideC_ = 0;
    bool emphOn_ = false;
    std::vector<float> scratch_;
};

}  // namespace sw::dy02
