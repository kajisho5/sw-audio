// SW MS04 Clipper — oversampled clipper whose knee morphs from hard clip to tape-like (spec: 仕様書 v1.0「MS04 Clipper」)
// Linear-phase FIR oversampling 4x/8x/16x (latency 48 samples for every factor). Mix is applied by sw::Shell.
#pragma once
#include "sw/oversample_fir.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include <array>
#include <vector>

namespace sw::ms04 {

enum ParamId { Drive, Ceiling, Knee, Mix, Oversample, GainMatch, Listen, kNumParams };

const std::vector<ParamSpec>& specs();

// normalised transfer curve (ceiling = 1). knee01: 0 hard, 0.5 soft (quadratic knee), 1 tape-like (tanh)
double clipCurve(double u, double knee01);

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return FirOversampler::kTapsPerPhase; }

private:
    static int factorIndex(double f) { return f < 6 ? 0 : (f < 12 ? 1 : 2); }
    double runOne(FirOversampler& os, double x, double ceil, double knee);
    double fs_ = 48000.0;
    std::array<double, kNumParams> target_{};
    std::array<std::array<FirOversampler, 3>, 2> os_{};
    LinearSmoother drive_, ceil_, knee_;
    int active_ = 1, previous_ = 1, fade_ = 0, fadeLen_ = 480;
    std::vector<double> buf_;
    std::array<std::vector<double>, 2> dly_{};
    int dpos_ = 0;
};

}  // namespace sw::ms04
