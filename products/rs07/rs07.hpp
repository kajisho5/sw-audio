// SW RS07 Mouth Noise — lip smacks and mouth clicks in the gaps between words (spec: 仕様書 v1.0「RS07 Mouth Noise」). Reported delay 512 samples.
//   RS04's detection and repair (sw/ar_repair.hpp: an order-32 AR model on the last 1024 samples, hop 128, least-squares interpolation), weighted by whether the voice is there:
//   a candidate counts only when it lies in a gap: the level around it (20 ms each side, inside the 1024-sample window) (the click's own samples left out) is more than 12 dB under the phrase peak (the level of the window, held 4 s, falling 3 dB/s).
//   Inside a word nothing is touched. Sensitivity Low / Mid / High: T = 6 / 4.5 / 3.5 x sigma of the excitation. Click size Small / Medium / Large: the longest click, 0.3 / 0.6 / 1 ms.
//   Freq skew -50 .. +50 %: tilts the excitation the detector looks at: + emphasises the high frequencies (e + s (e[n] - e[n-1])), - the low ones (e + |s| (e[n] + e[n-1])).
//   Fade 0.5 .. 10 ms: the repaired span is widened by half of it on each side (at most 60 samples), so the repair starts and ends on clean signal.
#pragma once
#include "sw/ar_repair.hpp"
#include "sw/param.hpp"
#include <array>
#include <vector>

namespace sw::rs07 {

enum ParamId { Sensitivity, ClickSize, FreqSkew, Fade, kNumParams };

const std::vector<ParamSpec>& specs();
constexpr int kLatency = 512, kHop = 128, kWin = 1024, kOrder = 32;

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return kLatency; }
    int clicksRepaired() const { return count_; }

private:
    struct Chan { std::vector<double> ring; double peakDb = -200.0; };
    void analyse(Chan& c, int64_t t);
    double fs_ = 48000.0;
    int count_ = 0, sinceHop_ = 0, lo_ = 0, hi_ = 0;
    int64_t t_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<Chan, 2> ch_;
    std::vector<double> w_, win_, e_, med_, tmp_;
    double a_[kOrder + 1] = {}, r_[kOrder + 1] = {};
    ArWork work_;
};

}  // namespace sw::rs07
