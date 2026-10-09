// SW LO02 Sub Gen — a sine one octave below the bass, phase-locked to it, zero delay (spec: 仕様書 v1.0「LO02 Sub Gen」)
// Mid (L+R)/2 -> 20 Hz high-pass -> LR4 low-pass at 2 x Range (detection band) -> Schmitt zero-crossing detector (hysteresis 10 % of the level).
// Every accepted rising crossing gives the period T (median-style smoothing, doubled-crossing rejection) and toggles a flip-flop (the divide-by-two).
// A sine oscillator at 2^(Tune/12) / (2T) is pulled onto the flip-flop edges (half of the phase error per edge, only while Tune = 0), so the sub is
// a pure sine whose zero crossings sit on the bass's. Level = G(Sub) x the band's level follower. Punch lifts the first ~40 ms after an onset by up to 2x.
// Output = Dry x input + sub on both channels.
#pragma once
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::lo02 {

enum ParamId { Sub, RangeHz, Tune, Punch, Dry, Unit, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double trackedHz() const { return period_ > 0 ? fs_ / period_ : 0.0; }   // detected input frequency (display / tests)

private:
    struct Lr4 {
        Svf a, b;
        void setup(double fc, double fs) { a.setup(Svf::Mode::LowPass, fc, fs, 0.70710678, 0); b.setup(Svf::Mode::LowPass, fc, fs, 0.70710678, 0); }
        double process(double x) { return b.process(a.process(x)); }
    };
    void updateFilters();
    void resetTracker();
    double fs_ = 48000.0, time_ = 0, lastFire_ = -1, period_ = 0, phase_ = 0, prev_ = 0, env_ = 0, fast_ = 0, slow_ = 0, trans_ = 0, quiet_ = 0, relDec_ = 0, fastDec_ = 0, transDec_ = 0;
    int flip_ = 0, rejects_ = 0;
    bool armed_ = false;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    Lr4 lp_;
    Svf hp_;
    LinearSmoother sub_, dry_;
    double tuneRatio_ = 1.0;
};

}  // namespace sw::lo02
