// SW DY04 Gate — 500-series gate / expander / ducker (spec: 仕様書 v1.0「DY04 Gate」, ranges: 04_parameters)
// Key = main input or the external sidechain, through optional key HPF / LPF. Listen outputs the key.
#pragma once
#include "sw/gate.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::dy04 {

enum ParamId { Threshold, Range, Attack, Hold, Release, Mode, KeyHpf, KeyLpf, KeyHpfHz, KeyLpfHz, Listen, kNumParams };

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
    bool isOpen() const { return gate_.isOpen(); }

private:
    void run(float** ch, int numCh, int n, const float* const* sc, int scCh);
    void applyGate();
    double fs_ = 48000.0;
    std::array<double, kNumParams> target_{};
    GateEngine gate_;
    std::array<Svf, 2> hp_{}, lp_{};
};

}  // namespace sw::dy04
