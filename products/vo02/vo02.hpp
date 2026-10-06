// SW VO02 Tune Rt — pitch correction for live singing (spec: 仕様書 v1.0「VO02 Tune Rt」). Wet signal only (Mix is the shared frame's, default 100 %); the track is mono (a stereo input is summed).
//   The same engine as VO01 (sw::PitchAnalyzer + sw::PsolaSynth) with a shorter setting: lowest pitch 110 Hz (the pitch window is shorter), grains of 1.5 periods, and PitchCorrector's
//   "weaken the correction on an unstable stretch" (a jump of more than 2.5 semitones between two marks: the next 4 marks are corrected at 30 % so that a wrong estimate is not sung out loud).
//   Key (12) and Scale (Maj / Min / Chr); Speed Slow .. Hard = 100 .. 0 ms (Skew k = 2, the knob runs backwards: its middle is 25 ms); Humanize 0 .. 10 (= 0 .. 100 % of VO01's);
//   Formant -3 .. +3 semitones (the vowel moves, the pitch does not). Reported delay: 1085 samples (22.6 ms at 48 kHz); nothing is shifted below 110 Hz or in unvoiced sounds.
#pragma once
#include "sw/param.hpp"
#include "sw/pitch_correct.hpp"
#include "sw/pitch_engine.hpp"
#include <array>
#include <vector>

namespace sw::vo02 {

enum ParamId { Key, Scale, Speed, Humanize, Formant, Mix, kNumParams };
enum ScaleId { Maj = 0, Min = 1, Chr = 2 };

const std::vector<ParamSpec>& specs();
PitchConfig engineConfig(double fs);

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const;
    const PitchAnalyzer& analyzer() const { return an_; }

private:
    void apply();
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    PitchAnalyzer an_;
    PsolaSynth synth_;
    PitchCorrector corr_;
};

}  // namespace sw::vo02
