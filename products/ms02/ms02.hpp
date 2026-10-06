// SW MS02 True Peak — safety limiter that also stops inter-sample peaks (spec: 仕様書 v1.0「MS02 True Peak」)
// Input gain -> look-ahead limiter (true-peak detection 4x/8x) -> optional TPDF dither.
#pragma once
#include "sw/dynamics.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace sw::ms02 {

enum ParamId { Gain, Ceiling, Release, LookaheadMs, TruePeak, Isp, Link, Dither, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { gain_.skip(1 << 30); }
    void process(float** ch, int numCh, int n);
    int latencySamples() const;  // desired; lookahead / true peak / ISP apply at prepare
    long long limitEvents() const { return lim_.limitEvents(); }  // EVO: inter-sample peak events

private:
    void applyLimiterSettings();
    double fs_ = 48000.0;
    std::array<double, kNumParams> target_{};
    PeakLimiter lim_;
    LinearSmoother gain_;
    int sustained_ = 0;
    uint32_t rng_ = 0x12345678u;
};

}  // namespace sw::ms02
