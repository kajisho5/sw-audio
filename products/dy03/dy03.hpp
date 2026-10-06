// SW DY03 Bus — VCA bus compressor (spec: 仕様書 v1.0「DY03 Bus 進化版」)
// Feed-forward, program detection, stepped Ratio/Attack/Release. Release Auto = two stages (100 ms / 1.2 s).
// EVO Punch keep: low (<150 Hz) and mid (1-5 kHz) onset detectors relax the gain reduction for 15 ms.
#pragma once
#include "sw/dynamics.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::dy03 {

enum ParamId { Threshold, Ratio, Attack, Release, Makeup, Mix, Knee, ScHpf, PunchKeep, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { makeup_.skip(1 << 30); }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double gainReductionDb() const { return gr_; }

private:
    void updateDynamics();
    bool autoRelease() const { return target_[Release] >= specs()[Release].steps.back(); }
    double fs_ = 48000.0, gr_ = 0.0, punch_ = 1.0, fastEnv_ = 0, slowEnv_ = 0;
    std::array<double, kNumParams> target_{};
    GainComputer comp_;
    Ballistics fast_, slow_;
    std::array<LevelDetector, 2> det_{};
    std::array<Svf, 2> hpf_{};
    Svf low_, midHp_, midLp_;
    bool hpfOn_ = false;
    int hold_ = 0;
    double envRel_ = 0, envSlow_ = 0, punchCoef_ = 0;
    LinearSmoother makeup_;
};

}  // namespace sw::dy03
