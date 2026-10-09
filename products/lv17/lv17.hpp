// SW LV17 Bus Comp — LIVE line bus compressor (spec: 仕様書 v1.0「LV17」, defaults: screen values in 04_parameters)
// Mode presets: Speech = RMS / 6 dB knee, Music = program / 4 dB knee, Band = peak / 2 dB knee.
// EVO: Mode is automatable, so LV27 Scene Sync can switch it with OBS scenes. Release Auto = 100 ms / 1.2 s stages.
#pragma once
#include "sw/dynamics.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include <array>
#include <vector>

namespace sw::lv17 {

enum ParamId { Mode, Threshold, Ratio, Attack, Release, Makeup, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { makeup_.skip(1 << 30); }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double gainReductionDb() const { return gr_; }   // the compression now (dB, <= 0), for the screen

private:
    void update();
    bool autoRelease() const { return target_[Release] >= specs()[Release].max; }
    double fs_ = 48000.0, gr_ = 0.0;
    std::array<double, kNumParams> target_{};
    GainComputer comp_;
    Ballistics fast_, slow_;
    std::array<LevelDetector, 2> det_{};
    LinearSmoother makeup_;
};

}  // namespace sw::lv17
