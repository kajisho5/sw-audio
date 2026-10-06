// SW ST06 Mono Low — makes the low end mono (spec: 仕様書 v1.0「ST06 Mono Low」). The whole signal is processed (no Mix); reported delay 0.
//   M = (L + R) / 2 is never touched; S = (L - R) / 2 goes through a high-pass of the chosen slope (6 / 12 / 24 / 48 dB/oct: Butterworth sections) at Frequency, then Side boost
//   (+-6 dB, a high shelf at Frequency x 1 octave: only above Frequency) and L = M + S, R = M - S, Output +-10 dB. The side's phase is the high-pass's, the mid's is none: below
//   Frequency the output is the mid alone (mono), above it both. Listen (monitoring, not automatable): plays what the mono-ing removes: the side below Frequency (the low-pass of the same order as the high-pass), on both channels.
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::st06 {

enum ParamId { Frequency, Slope, SideBoost, Output, Listen, kNumParams };

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
    void update();
    double fs_ = 48000.0, outG_ = 1.0, outT_ = 1.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<Svf, 4> hp_{}, lp_{};   // up to four sections (2 x 24 dB/oct = 48), the first-order case uses a one-pole (lp_: Listen's low-pass of the same order)
    Svf shelf_;
    double hpOne_ = 0.0, hpState_ = 0.0, freq_ = -1, slope_ = -1, boost_ = -99;
    int sections_ = 2;
};

}  // namespace sw::st06
