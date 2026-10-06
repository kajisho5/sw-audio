// SW CS01 Inductor Strip — iron preamp + EQ04-circuit EQ + feedback compressor (spec: 仕様書 v1.0「CS01 Inductor Strip」)
// Pre: frequency-dependent iron saturation (lows saturate most), 2x OS. Comp: feedback detection, program-dependent
// attack 2..20 ms, Mix = parallel blend of the compressor only. Order: EQ first / Comp first (10 ms crossfade).
// EVO Mic profile (rule table that writes HPF / EQ / comp starting values) comes with the UI.
#pragma once
#include "sw/dynamics.hpp"
#include "sw/oversample.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::cs01 {

enum ParamId { Drive, Hpf, High, MidFreq, Mid, Low, Thresh, Ratio, Release, Order, Mix, Output, Link, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }

private:
    struct Eq { OnePole hp1; Svf hp2, low, mid, high; };
    struct Comp { std::array<LevelDetector, 2> det{}; std::array<double, 2> grDb{}, lastOut{}; };
    struct Chain { std::array<Eq, 2> eq{}; Comp comp; };
    void updateEq(int ramp);
    double eqSample(Eq& e, double x, double hpOn);
    void compSample(Comp& c, double* x, int nch);
    void runChain(Chain& ch, bool eqFirst, double* x, int nch, double hpOn);
    double fs_ = 48000.0;
    std::array<double, kNumParams> target_{};
    LinearSmoother hpfF_, hpfOn_, high_, midF_, mid_, low_, drive_, mix_, order_;
    std::array<Chain, 2> chain_{};  // [0] EQ -> Comp, [1] Comp -> EQ
    std::array<Oversampler2x, 2> os_{};
    std::array<std::array<OnePole, 2>, 2> split_{};  // [channel][oversampled half] low split for the iron
    GainComputer gc_;
    double relCoef_ = 0;
};

}  // namespace sw::cs01
