// SW MS01 Maximizer — two-stage look-ahead maximizer with Lock (spec: 仕様書 v1.0「MS01 Maximizer 進化版」)
// Gain -> slow stage (density; Character X = ratio 1..4 and knee 0..12 dB, Y = attack 1..30 ms) -> fast stage (sw::PeakLimiter, 2 ms
// look-ahead, true peak 4x) -> TPDF dither. Lock: measures the integrated loudness of the output (10 s memory) and moves Gain to Target
// (time constant 10 s); after >= 30 s, within 0.3 LU of Target and steady (< 0.15 LU change over 5 s) it holds the value (readable with gainDb(); the plugin layer writes it back).
//   Low lat (the last parameter, spec: common function): the look-ahead shrinks from 2 ms to 0.5 ms (the true-peak interpolation stays the FIR one: 40 samples instead of the spec's about 24 with an IIR interpolation).
#pragma once
#include "sw/dynamics.hpp"
#include "sw/loudness.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace sw::ms01 {

enum ParamId { Gain, Target, Lock, CharX, CharY, Ceiling, Release, Stereo, TruePeak, Dither, LowGuard, LowLat, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { gain_.skip(1 << 30); }
    void process(float** ch, int numCh, int n);
    int latencySamples() const;  // desired; look-ahead / true peak / Low lat apply at prepare
    double slowReductionDb() const { return slowGr_; }
    double fastReductionDb() const { return lim_.gainReductionDb(); }
    bool locked() const { return locked_; }
    double gainDb() const { return gainDb_; }   // effective input gain (Lock writes it)
    double integratedLufs() const { return meter_.integrated(); }

private:
    void applyLimiter();
    void applySlow();
    void restartLock();
    double fs_ = 48000.0, gainDb_ = 0, slowGr_ = 0, slowAtt_ = 0, slowRel_ = 0, envC_ = 0, lockClock_ = 0, lockTick_ = 0;
    bool locked_ = false;
    std::array<double, kNumParams> target_{};
    PeakLimiter lim_;
    LinearSmoother gain_;
    GainComputer slow_;
    std::array<Ballistics, 2> slowBall_{};
    std::array<double, 2> env_{};
    std::array<Svf, 2> guard_{};
    IntegratedLoudness meter_;
    std::array<double, 6> hist_{};   // integrated loudness once a second, for the stability check
    int histPos_ = 0, histFill_ = 0;
    double histClock_ = 0;
    std::vector<float> chunk_;
    uint32_t rng_ = 0x12345678u;
};

}  // namespace sw::ms01
