// SW DY12 Parallel — parallel compression and upward compression in one box (spec: 仕様書 v1.0「DY12 Parallel」)
// Squashed side: feed-forward 10:1 (threshold = -4 x Squash dBFS), automatic makeup so that a -12 dBFS RMS reference level keeps
// its level, Tone = first-order tilt (+-6 dB around 1 kHz) on this side only. Dry side: Upward lifts what is quiet (up to +12 dB).
// Out = (1 - Blend) x dry(with Upward) + Blend x squashed. No product Mix (Blend plays that role).
#pragma once
#include "sw/dynamics.hpp"
#include "sw/param.hpp"
#include <array>
#include <vector>

namespace sw::dy12 {

enum ParamId { Squash, Blend, Upward, Tone, Speed, Output, kNumParams };

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
    double upwardDb() const { return lift_; }

private:
    void updateTiming();
    static double upwardWeight(double levelDb);
    double fs_ = 48000.0, gr_ = 0, lift_ = 0, liftAtt_ = 0, liftRel_ = 0, toneC_ = 0, makeupDb_ = 0;
    std::array<double, kNumParams> target_{};
    GainComputer comp_;
    Ballistics fast_, slow_;
    bool autoSpeed_ = false;
    std::array<LevelDetector, 2> sq_{}, up_{};
    std::array<double, 2> lp_{};
};

}  // namespace sw::dy12
