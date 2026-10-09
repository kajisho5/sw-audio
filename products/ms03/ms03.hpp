// SW MS03 Multiband Limit — four-band look-ahead limiter, one ceiling for the sum (spec: 仕様書 v1.0「MS03 Multiband Limit」)
// LR4 split (sw::Lr4Split4) -> per band: Gain, soft pre-clip (Dense only), look-ahead limiter (2 ms, sw::PeakLimiter) -> sum ->
// link stage (1 ms look-ahead limiter on the sum; with Link bands the reduction it asks for is shared out by the bands' current energy,
// otherwise it is applied to the sum as a whole) -> final true-peak limiter (4x, 0.5 ms) that always keeps Out ceiling.
//   Low lat (the last parameter, spec: common function): the band and link look-ahead shrink to 0.5 ms (the delay 184 -> 88 samples @48 kHz). Like every setting that changes the delay it applies at the next prepare().
#pragma once
#include "sw/dynamics.hpp"
#include "sw/lr4split.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include <array>
#include <vector>

namespace sw::ms03 {

enum BandParam { BGain, BCeiling, BRelease };
constexpr int band(int n, int k) { return n * 3 + k; }   // n = 0..3
enum ParamId { X1 = 12, X2, X3, OutCeiling, Char, Link, LowLat, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { for (auto& g : gain_) g.skip(1 << 30); }
    void process(float** ch, int numCh, int n);
    int latencySamples() const;
    double bandReductionDb(int b) const { return bandLim_[static_cast<size_t>(b)].gainReductionDb(); }

private:
    static constexpr int kChunk = 256;
    void applyBands();
    void applyCrossovers();
    void runChunk(float** ch, int nch, int n);
    double fs_ = 48000.0;
    int la1_ = 96, la2_ = 48, la3_ = 24;
    bool low_ = false;   // Low lat as prepared
    std::array<double, kNumParams> target_{};
    Lr4Split4 split_;
    std::array<PeakLimiter, 4> bandLim_;
    PeakLimiter linkLim_, finalLim_;
    std::array<LinearSmoother, 4> gain_;
    std::array<std::array<std::vector<float>, 2>, 4> band_;       // band signals of the chunk
    std::array<std::array<std::vector<float>, 2>, 4> delayed_;    // bands delayed by the link stage's look-ahead (ring, la2_ long)
    std::array<std::vector<float>, 2> sum_, sumLimited_;
    std::array<double, 4> energy_{};
    double energyC_ = 0, gPrev_ = 1.0;
    int dpos_ = 0;
};

}  // namespace sw::ms03
