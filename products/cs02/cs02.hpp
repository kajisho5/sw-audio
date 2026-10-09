// SW CS02 Console Strip — console channel strip (spec: 仕様書 v1.0「CS02 Console Strip」)
// Input HPF 18 dB/oct / LPF 12 dB/oct -> [Dyn: gate (0.1 / 20 / 100 ms) -> VCA feed-forward comp (3 ms attack)]
// and [EQ: 100 Hz shelf, 600 Hz / 3 kHz bells Q 1, 10 kHz shelf] in the Route order -> fader (0 dB at 75 %).
// Thresholds: 0 dB = -18 dBFS (spec proposal). EVO gate bleed learning (B) comes with the UI.
#pragma once
#include "sw/bleed_learner.hpp"
#include "sw/dynamics.hpp"
#include "sw/gate.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::cs02 {

enum ParamId { Ratio, Thresh, Release, GateThresh, GateRange, Hpf, Lpf, Hf, Hmf, Lmf, Lf, Route, Fader, Link, Unit, KeyHpf, KeyLpf, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    // Learn (EVO, class B; the same learner as DY04's): the input is listened to for at most kLearnSeconds (or until learn() is called again); then the Gate threshold goes between the wanted hits and the bleed
    // and the gate's key filters (KeyHpf / KeyLpf: internal values, no knobs) to the edges of the hits' band. The core writes the three to the host (takeParamWrite: bit 0 begin, 1 value, 2 end). Audio thread.
    static constexpr double kLearnSeconds = 30.0;
    void learn();
    bool learning() const { return learner_.learning(); }
    double learnProgress() const { return learner_.progress(); }
    int learnOnsets() const { return learner_.onsets(); }
    bool learnedOk() const { return learnedOk_; }
    int takeParamWrite(int& id, double& plain);

private:
    struct Eq { Svf lf, lmf, hmf, hf; };
    struct Dyn { std::array<GateEngine, 2> gate{}; std::array<LevelDetector, 2> det{}; std::array<double, 2> gr{}; std::array<Svf, 2> keyHp{}, keyLp{}; };
    struct Chain { std::array<Eq, 2> eq{}; Dyn dyn; };
    void updateFilters(int ramp);
    void updateDyn();
    void dynSample(Dyn& d, double* x, int nch);
    void runChain(Chain& c, bool dynFirst, double* x, int nch);
    double fs_ = 48000.0;
    std::array<double, kNumParams> target_{};
    LinearSmoother hpfF_, hpfOn_, lpfF_, lpfOn_, hf_, hmf_, lmf_, lf_, fader_, route_;
    std::array<OnePole, 2> hp1_{};
    std::array<Svf, 2> hp2_{}, lp_{};
    std::array<Chain, 2> chain_{};  // [0] Dyn -> EQ, [1] EQ -> Dyn
    GainComputer gc_;
    double atk_ = 0, rel_ = 0;
    void applyLearned(const BleedLearner::Result& r);
    void updateKey();
    bool keyHpOn_ = false, keyLpOn_ = false;     // the gate's key filters (learned; at their ends of the range they are bypassed)
    BleedLearner learner_;
    std::array<std::pair<int, double>, 3> writes_{};
    int nWrites_ = 0, writeAt_ = 0;
    bool wasLearning_ = false, learnedOk_ = false;
};

}  // namespace sw::cs02
