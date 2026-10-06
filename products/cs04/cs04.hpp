// SW CS04 Modular Strip — six modules (Gate, EQ, Comp, Saturate, De-ess, Limit), each with On, in any order
// (spec: 仕様書 v1.0「CS04 Modular Strip」). The order is one non-automatable choice among 720 (saved with the state).
// Latency: 1 ms look-ahead while Limit is on (48 samples at 48 kHz), otherwise 0. Only the EQ module is on screen;
// the other modules' controls are spec proposals. EVO order suggestion (by source type) comes with the UI.
#pragma once
#include "sw/dynamics.hpp"
#include "sw/gate.hpp"
#include "sw/oversample.hpp"
#include "sw/param.hpp"
#include "sw/saturate.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::cs04 {

enum ParamId { GateOn, GateThresh, GateRange, GateRelease, EqOn, EqLow, EqMidFreq, EqMid, EqHigh, EqOut, CompOn, CompThresh, CompRatio, CompAttack, CompRelease, CompMakeup,
               SatOn, SatDrive, SatMix, DeessOn, DeessFreq, DeessThresh, DeessRange, LimitOn, LimitCeiling, LimitRelease, Order, kNumParams };
enum Module { MGate, MEq, MComp, MSat, MDeess, MLimit, kModules };

const std::vector<ParamSpec>& specs();
std::array<int, kModules> orderFromIndex(int index);       // factorial number system (Lehmer code)
int indexFromOrder(const std::array<int, kModules>& order);

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const;

private:
    void module(int m, double* x, int nch);
    void updateFilters(int ramp);
    double fs_ = 48000.0;
    std::array<double, kNumParams> target_{};
    std::array<int, kModules> order_{0, 1, 2, 3, 4, 5};
    int pendingOrder_ = 0, dipLeft_ = 0, dipLen_ = 240;
    bool limitActive_ = false;
    std::array<LinearSmoother, kModules> on_{};
    LinearSmoother eqLow_, eqMidF_, eqMid_, eqHigh_, eqOut_, makeup_, satMix_;
    // module state
    std::array<GateEngine, 2> gate_{};
    struct EqCh { Svf low, mid, high; };
    std::array<EqCh, 2> eq_{};
    std::array<LevelDetector, 2> compDet_{};
    GainComputer compGc_;
    Ballistics compBall_;
    std::array<Oversampler2x, 2> satOs_{};
    Saturator sat_;
    struct DeessSvf { double ic1 = 0, ic2 = 0; };  // TPT SVF state; lp + k*sqrt(G)*bp + G*hp is a high shelf that is exact identity at G = 1
    std::array<DeessSvf, 2> deessSvf_{};
    double deessA1_ = 0, deessA2_ = 0, deessA3_ = 0;
    double deessEnv_ = 0, deessAtk_ = 0, deessRel_ = 0, instGain_ = 1, instRel_ = 0;
    PeakLimiter limiter_;
    std::vector<float> lim_[2];
};

}  // namespace sw::cs04
