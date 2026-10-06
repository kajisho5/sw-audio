// SW DY08 Clean — transparent digital compressor (spec: 仕様書 v1.0「DY08 Clean」)
// Feed-forward, gain computer in dB, linked stereo detection. Mix is applied by sw::Shell.
#pragma once
#include "sw/dynamics.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::dy08 {

enum ParamId { Threshold, Ratio, Knee, Attack, Release, AutoRelease, Makeup, Mix, ScHpf, Detector, Lookahead, Sidechain, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n) { run(ch, numCh, n, nullptr, 0); }
    // External sidechain: used for detection when the Sidechain parameter is External
    void processWithSidechain(float** ch, int numCh, int n, const float* const* sc, int scCh) { run(ch, numCh, n, sc, scCh); }
    int latencySamples() const;           // desired latency (Lookahead); applied at prepare
    void setTempo(double bpm);
    double effectiveReleaseMs() const;
    double gainReductionDb() const { return grDb_; }

private:
    void run(float** ch, int numCh, int n, const float* const* sc, int scCh);
    void updateDynamics();
    double fs_ = 48000.0, tempo_ = 0.0, grDb_ = 0.0;
    std::array<double, kNumParams> target_{};
    GainComputer comp_;
    Ballistics ball_;
    std::array<LevelDetector, 2> det_{};
    std::array<Svf, 2> hpf_{};
    bool hpfOn_ = false;
    LinearSmoother makeup_;
    int la_ = 0, dpos_ = 0;
    std::array<std::vector<float>, 2> delay_{};
};

}  // namespace sw::dy08
