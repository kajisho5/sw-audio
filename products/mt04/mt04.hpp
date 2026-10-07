// SW MT04 Phase Scope — a stereo phase scope and correlation meter (spec: 仕様書 v1.0「MT04 Phase Scope」). The signal passes unchanged; reported delay 0; no Delta / Auto gain.
//   Scope points: x = (L - R) / sqrt 2, y = (L + R) / sqrt 2 (mono is a vertical line), one point in 16 samples, kept for Persistence seconds (0.1 .. 5, at most 12000 points); Zoom 1 / 2 / 4 / 8 x is the screen's scale
//   (scopePoint() applies it). Correlation: the coefficient sum(LR) / sqrt(sum(LL) sum(RR)) over an exponential window of 300 ms, broadband and in 8 octave bands (62.5 Hz .. 8 kHz, a band-pass of Q 1.4 on both
//   channels). EVO (class A): a band that has been below zero for 0.7 s (up to the 1 s of the spec's worry) is flagged in warnings() (bit i = band i) until its correlation is back over +0.1; mono-compatibility is
//   the broadband value read in dB of cancellation (summedLossDb(): the level of L + R against the power sum of L and R).
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::mt04 {

enum ParamId { Persistence, Zoom, kNumParams };
constexpr int kBandsN = 8, kMaxPoints = 12000, kPointStep = 16;

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double correlation() const;                       // broadband, -1 .. +1
    double bandCorrelation(int band) const;
    int warnings() const { return warn_; }
    double bandHz(int band) const { return 62.5 * std::pow(2.0, band); }
    double summedLossDb() const;                       // 10 log10( sum (L+R)^2 / (sum L^2 + sum R^2) ): 0 for uncorrelated, +3 mono, large negative for opposite phase
    int pointCount() const { return count_; }
    void scopePoint(int index, double& x, double& y) const;   // 0 = the oldest kept, with Zoom applied

private:
    struct Band { Svf l, r; double ll = 0, rr = 0, lr = 0, below = 0.0; };
    double fs_ = 48000.0, ll_ = 0, rr_ = 0, lr_ = 0, sumsq_ = 0;
    int warn_ = 0, head_ = 0, count_ = 0, sinceP_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<Band, kBandsN> bands_;
    std::vector<float> px_, py_;
};

}  // namespace sw::mt04
