// SW LV06 Stream master — the last stage of a stream (spec: 仕様書 v1.0「LV06 Stream master」). Reported delay 0.
//   Slow auto gain: the 3 s loudness (BS.1770 K-weighted, sw::LoudnessMeter::shortTerm) of the input is compared with Target; the gain moves towards (Target - loudness) at Ride speed
//   (Slow 0.5 / Medium 1.5 / Fast 4 dB per second), up to Max boost and down to -12 dB (design value: the spec gives only the boost). While the 400 ms loudness is under -50 LUFS (silence; "Dead air" is shown after 2 s under -60 LUFS short-term) and during the first second the gain stays where it is.
//   Then the Zero-latency peak limiter of LV04 (instantaneous, 50 ms release, linked): the sample peak never goes over Ceiling. Mono safe: below 150 Hz (Linkwitz-Riley 4th order) the signal is made mono, and when the channels are out of phase (correlation under 0) the side is
//   pulled down by up to 12 dB.
//   Targets: Stream -14, Podcast -16, Broadcast -24, Custom (the Custom parameter, -30..-5 LUFS).
#pragma once
#include "sw/loudness.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::lv06 {

enum ParamId { Target, Custom, Ride, MaxBoost, Ceiling, MonoSafe, kNumParams };
enum TargetId { Stream = 0, Podcast = 1, Broadcast = 2, CustomT = 3 };

const std::vector<ParamSpec>& specs();
double targetLufs(int target, double custom);

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double autoGainDb() const { return gDb_; }
    double loudnessLufs() const { return meter_.shortTerm(); }
    bool deadAir() const { return dead_; }
    double limiterReductionDb() const { return 20.0 * std::log10(std::max(lim_, 1e-9)); }

private:
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    LoudnessMeter meter_;
    double gDb_ = 0, lim_ = 1, seen_ = 0, quiet_ = 0, corr_ = 0, pLR_ = 0, pLL_ = 0, pRR_ = 0, side_ = 1;
    bool dead_ = false;
    struct X { Svf lpA, lpB, hpA, hpB; };
    std::array<X, 2> x_{};
};

}  // namespace sw::lv06
