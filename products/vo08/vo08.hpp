// SW VO08 Breath — lower, remove, or only mark the breaths between phrases (spec: 仕様書 v1.0「VO08 Breath」). Reported delay 1024 samples (the look-ahead that lets the gain fade in before the breath).
//   Detection (rules, the first version of the spec): every 4 ms frame (a 12 ms analysis window) is a breath frame when it is (1) noise-like: the zero-crossing rate is above 0.16 / 0.12 / 0.09 (Sensitivity
//   Low / Mid / High) and the normalised autocorrelation peak (lags 40 .. 600 samples at 48 kHz, scaled with the rate) is under 0.6 (no voiced sound; the autocorrelation runs on a copy decimated to about 12 kHz), (2) quiet against the phrase: the frame level is
//   more than 20 / 15 / 10 dB under the phrase peak (held for 4 s, falling 3 dB/s, a held value of the frame levels) and (3) not silence (above -80 dBFS). A breath starts after 2 breath frames in a row (about 16 ms after its first sample) and ends at the first frame that is none.
//   Mode: Reduce = the gain goes down by Reduction x the Keep factor; Remove = down by 60 dB (Reduction is not used); Mark only = the audio is untouched and breathActive() / breathCount() tell the display.
//   Keep (design values): Natural x0.6 of Reduction (a breath is still heard), Less x1.0, None x1.5 (capped at -40 dB). Fade 1 .. 50 ms: the time the gain takes to go down the whole way (linear in dB); coming back it takes at most 10 ms, so that the next phrase is never covered.
//   The detector runs on the input, the gain acts on the signal delayed by 1024 samples: the decision arrives about 5 ms before the breath's first sample leaves the plugin: a fade longer than that is partly inside the breath.
#pragma once
#include "sw/param.hpp"
#include <array>
#include <vector>

namespace sw::vo08 {

enum ParamId { Mode, Reduction, Sensitivity, Keep, Fade, kNumParams };
enum ModeId { Reduce = 0, Remove = 1, MarkOnly = 2 };
enum KeepId { Natural = 0, Less = 1, None = 2 };

const std::vector<ParamSpec>& specs();
constexpr int kLatency = 1024;

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return kLatency; }
    bool breathActive() const { return inBreath_; }   // the input is in a breath now (Mark only: the display)
    int breathCount() const { return count_; }
    double gainDb() const { return gainDb_; }

private:
    void frame();
    double depthDb() const;
    double fs_ = 48000.0, peakDb_ = -200.0, gainDb_ = 0.0;
    int count_ = 0, run_ = 0, hop_ = 192, win_ = 576, lagLo_ = 40, lagHi_ = 600, dec_ = 4, dpos_ = 0;
    bool inBreath_ = false;
    std::array<double, kNumParams> target_{};
    std::array<std::vector<float>, 2> dly_;
    std::vector<float> ring_;   // mono analysis ring
    std::vector<double> tmp_, decd_;
    int rpos_ = 0, since_ = 0;
};

}  // namespace sw::vo08
