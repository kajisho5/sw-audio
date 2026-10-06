// SW LV16 Live Gate — LIVE line gate / ducker (spec: 仕様書 v1.0「LV16」, defaults: screen values in 04_parameters)
// EVO Key HPF: a fixed 120 Hz 24 dB/oct key filter keeps stage rumble from opening the gate (learning: later).
#pragma once
#include "sw/gate.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::lv16 {

enum ParamId { Mode, Threshold, Range, Hold, Release, KeyHpf, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n) { run(ch, numCh, n, nullptr, 0); }
    void processWithSidechain(float** ch, int numCh, int n, const float* const* sc, int scCh) { run(ch, numCh, n, sc, scCh); }
    int latencySamples() const { return 0; }

private:
    void run(float** ch, int numCh, int n, const float* const* sc, int scCh);
    void applyGate();
    double fs_ = 48000.0;
    std::array<double, kNumParams> target_{};
    GateEngine gate_;
    std::array<std::array<Svf, 2>, 2> hp_{};  // [channel][stage]
};

}  // namespace sw::lv16
