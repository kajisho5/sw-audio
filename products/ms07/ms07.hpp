// SW MS07 Dither — TPDF dither + requantization with error-feedback noise shaping (spec: 仕様書 v1.0「MS07 Dither」)
// Shapes: noise transfer (1 - z^-1)^N, N = 1..4 (Light..Ultra). FIR noise transfer -> unconditionally stable.
// EVO Auto blank: after 1024 samples of exact digital silence the output is exact silence; dither fades back in 2 ms.
#pragma once
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace sw::ms07 {

enum ParamId { Bits, Shape, Output, Blank, kNumParams };

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
    double tpdf();
    double fs_ = 48000.0;
    std::array<double, kNumParams> target_{};
    LinearSmoother gain_;
    struct Ch { std::array<double, 4> e{}; int zeroRun = 0; double ditherAmp = 1.0; };
    std::array<Ch, 2> ch_{};
    uint32_t rng_ = 0x9E3779B9u;
};

}  // namespace sw::ms07
