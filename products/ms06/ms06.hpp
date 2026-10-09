// SW MS06 Master Chain — EQ / Comp / Saturate / Width / Limit in any order, each with On and a per-stage Gain match
// (spec: 仕様書 v1.0「MS06 Master Chain」). The order is one non-automatable choice among 120 (saved with the state). Stages run on
// 256-sample chunks in the chosen order, so the look-ahead limiter can sit anywhere in the chain. Latency: the limiter's (72 + 16 at
// 48 kHz) while Limit was on at prepare, otherwise 0. Gain match: every stage output is brought to the loudness of the chain input
// (K-weighted, 3 s) -> whatever is switched on, off or moved, the overall loudness stays put. Reference A/B: monitoring switch only;
// loading and aligning a reference comes with UT03 / the screen.
#pragma once
#include "sw/drive.hpp"
#include "sw/dynamics.hpp"
#include "sw/loudness.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <cmath>
#include <vector>

namespace sw::ms06 {

constexpr int kStages = 5;
enum Stage { SEq, SComp, SSat, SWidth, SLimit };
enum ParamId { EqOn, EqTilt, EqLow, EqHigh, EqBell, EqBellFreq, CompOn, CompThresh, CompRatio, CompAttack, CompRelease, CompMix, SatOn, SatDrive, SatMix,
               WidthOn, Width, MonoBelow, LimitOn, LimitGain, LimitCeiling, LimitRelease, GainMatch, Order, RefAB, Oversample, kNumParams };

const std::vector<ParamSpec>& specs();
std::array<int, kStages> orderFromIndex(int index);   // Lehmer code
int indexFromOrder(const std::array<int, kStages>& order);

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const;
    double matchDb(int stage) const { return match_[static_cast<size_t>(stage)]; }
    double compReductionDb() const { return compGr_; }
    // the meters of the screen's top line, on the chain's output: short-term loudness (K-weighted, 3 s, LUFS; -200 while silent), the true peak since the start / resetMeters() (dBTP; -200: none yet)
    // and the loudness range (LU, EBU Tech 3342)
    double outShortTermLufs() const { return outShort_; }
    double outTruePeakDb() const { return outTp_ > 1e-9 ? 20.0 * std::log10(outTp_) : -200.0; }
    double outRangeLu() const { return lra_.range(); }
    void resetMeters();

private:
    static constexpr int kChunk = 256;   // the largest piece a stage works on at once (a run is never longer than kRun)
    static constexpr int kRun = 32;      // the control grid: gain match and its ramps are decided every 32 samples of the stream, not of the host's blocks
    static constexpr int kFade = 256;    // an order change: this many samples fading out, then the same fading in
    void runChunk(float** ch, int nch, int n);
    void controlBlock();
    void stageEq(int nch, int n); void stageComp(int nch, int n); void stageSat(int nch, int n); void stageWidth(int nch, int n); void stageLimit(int nch, int n);
    void updateEq(int ramp);
    void measureOut(float** ch, int nch, int n);
    double fs_ = 48000.0, compGr_ = 0, outShort_ = -200.0, outTp_ = 0.0;
    LoudnessMeter outMeter_; LoudnessRange lra_; std::array<TruePeakDetector, 2> outTpd_; long long outSamples_ = 0; int sinceLra_ = 0;   // the output meters
    std::array<double, kNumParams> target_{};
    std::array<int, kStages> order_{0, 1, 2, 3, 4};
    int pendingOrder_ = 0, fade_ = 0, fadePos_ = 0;   // fade_: 0 none, 1 fading out into the new order, 2 fading in (fadePos_ samples into it)
    int ph_ = 0;                                       // samples into the current control block (persistent: the grid is the stream's)
    bool limitActive_ = false, eqDirty_ = true;
    std::array<std::vector<double>, 2> buf_, dry_;
    std::array<LinearSmoother, kStages> on_;
    LinearSmoother compMix_, satMix_;
    // EQ
    struct EqCh { Svf tiltLo, tiltHi, low, high, bell; };
    std::array<EqCh, 2> eq_{};
    // Comp
    std::array<LevelDetector, 2> det_{};
    GainComputer gc_;
    Ballistics fast_, slow_;
    // Sat
    std::array<DriveStage, 2> sat_{};
    // Width
    std::array<Svf, 2> sideHp_{}, midLp_{}, midHp_{};   // Mono below: LR4 high-pass on the sides, the same crossover's all-pass on the mid
    // Limit
    PeakLimiter lim_;
    // gain match
    std::array<std::array<KWeighting, 2>, kStages + 1> kw_{};
    std::array<double, kStages + 1> ms_{}, acc_{};      // K-weighted power: the smoothed value per tap, the sum of the control block being played
    std::array<double, kStages> match_{}, gA_{}, gB_{};  // the correction (dB) per stage; its gain (linear) at the start and at the end of the ramp over the control block
    double msC_ = 0, matchC_ = 0;
};

}  // namespace sw::ms06
