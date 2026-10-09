// SW RS05 Declip — restores peaks that were cut off at a ceiling (spec: 仕様書 v1.0「RS05 Declip」). Reported delay 1024 samples.
//   A clipped run = 2 or more consecutive samples (up to 256) at or beyond the ceiling on one side. The ceiling is Threshold (-3 .. 0 dB); Detect On: the ceiling is read from the samples
//   instead: when the last 2048 samples pile up at their maximum (4 or more runs of 2 or more samples equal to it to 1e-4, max above 0.05) that maximum x 0.995 is the ceiling (smoothed; it is kept for 1 s,
//   then Threshold applies again). Per hop of 256 samples and channel an AR model (order 16 / 32 / 64 for Quality Low / Mid / High) is fitted to the last 2048 samples (sw/ar_repair.hpp); the
//   clipped samples of the runs that are about to leave the plugin are replaced by the least-squares interpolation, pushed back out to the ceiling where it falls below it (a clipped peak never
//   comes out lower than the ceiling) and limited to the ceiling + 9 / 6 / 3 dB (Smooth Low / Mid / High: the larger the smoother). Makeup (-12 .. 0 dB, the default -3) is a gain on the output
//   so that the restored peaks, which go above the ceiling, have room.
#pragma once
#include "sw/ar_repair.hpp"
#include "sw/param.hpp"
#include <array>
#include <vector>

namespace sw::rs05 {

enum ParamId { Threshold, Quality, Makeup, Smooth, Detect, kNumParams };

const std::vector<ParamSpec>& specs();
constexpr int kLatency = 1024, kHop = 256, kWin = 2048, kMaxRun = 256;

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return kLatency; }
    int runsRestored() const { return count_; }
    double ceilingUsed() const { return used_; }   // the ceiling (linear) of the last analysis of channel 0
    // for the screen (audio thread): the last kScopeWin samples that left the plug-in on channel 0 in kScopeBins bins (the sample with the largest size in each bin, oldest first):
    // `in` = as they came in, `out` = after the repair (before Makeup). Zeros until that much has left.
    static constexpr int kScopeBins = 64, kScopeWin = 1024;
    void scope(double* in, double* out) const;
    double scopeMs() const { return 1000.0 * kScopeWin / fs_; }

private:
    struct Chan { std::vector<double> ring, raw; double autoLevel = 0.0; int autoAge = 1 << 30; };
    void analyse(Chan& c, int64_t t);
    double fs_ = 48000.0, used_ = 1.0;
    int count_ = 0, sinceHop_ = 0;
    int64_t t_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<Chan, 2> ch_;
    struct Run { int s, m; bool pos; };
    std::vector<double> w_, win_, tmp_;
    std::vector<Run> runsAll_;
    double a_[65] = {}, r_[65] = {};
    ArWork work_;
};

}  // namespace sw::rs05
