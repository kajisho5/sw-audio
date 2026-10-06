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
#include <vector>

namespace sw::ms06 {

constexpr int kStages = 5;
enum Stage { SEq, SComp, SSat, SWidth, SLimit };
enum ParamId { EqOn, EqTilt, EqLow, EqHigh, EqBell, EqBellFreq, CompOn, CompThresh, CompRatio, CompAttack, CompRelease, CompMix, SatOn, SatDrive, SatMix,
               WidthOn, Width, MonoBelow, LimitOn, LimitGain, LimitCeiling, LimitRelease, GainMatch, Order, RefAB, kNumParams };

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

private:
    static constexpr int kChunk = 256;
    void runChunk(float** ch, int nch, int n);
    void stageEq(int nch, int n); void stageComp(int nch, int n); void stageSat(int nch, int n); void stageWidth(int nch, int n); void stageLimit(int nch, int n);
    void updateEq(int ramp);
    double fs_ = 48000.0, compGr_ = 0;
    std::array<double, kNumParams> target_{};
    std::array<int, kStages> order_{0, 1, 2, 3, 4};
    int pendingOrder_ = 0, fade_ = 0;           // fade_: 0 none, 1 fading out into the new order, 2 fading in
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
    int limSustained_ = 0;
    // gain match
    std::array<std::array<KWeighting, 2>, kStages + 1> kw_{};
    std::array<double, kStages + 1> ms_{};
    std::array<double, kStages> match_{}, matchPrev_{};
    double msC_ = 0, matchC_ = 0;
};

}  // namespace sw::ms06
