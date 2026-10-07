// SW LV03 Channel — a live channel strip (spec: 仕様書 v1.0「LV03 Channel」). Reported delay 0 (the de-esser has no look-ahead).
//   Order: Trim (and Ø) -> HPF -> Gate (hysteresis, 1 ms attack, 100 ms hold, 200 ms release; key = the HPF output, linked) -> EQ (Low shelf 100 Hz, Mid bell with Q 1, High shelf 8 kHz)
//   -> Feedback guard (4 LIVE slots of sw::FeedbackGuard, Mid sensitivity, -12 dB at most, 1/10 octave, 8 s release; Off = no analysis) -> Comp (linked, program detector, 6 dB knee, 10 ms / 120 ms, no make-up: Out is the make-up)
//   -> De-ess (a moving high shelf (corner 0.7 x Freq) cut by the level of the band above Freq: what is over -34 dBFS counts 0.8 dB per dB up to Amount; 1 ms / 40 ms; the shelf is updated every 16 samples when it moves by more than 0.05 dB) -> Out.
//   EVO (class A): Mic. Choosing a type writes every other parameter (except Out) in one go through takeParamWrite (sixteen gestures; a value set by hand or loaded with the project after it cancels that write).
//   Copy / Paste between channels needs the screen (it reads and writes the whole parameter set): not here.
#pragma once
#include "sw/dynamics.hpp"
#include "sw/feedback.hpp"
#include "sw/gate.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <utility>
#include <vector>

namespace sw::lv03 {

enum ParamId { Mic, Trim, Hpf, Phase, GateThresh, GateRange, EqLow, EqMid, EqHigh, EqMidF, FbGuard, CompThresh, CompRatio, DeessAmount, DeessFreq, Out, kNumParams };
enum MicId { Handheld = 0, Lavalier = 1, Headset = 2, Podium = 3 };

const std::vector<ParamSpec>& specs();
// the parameters a Mic type writes: (id, value), all but Mic and Out
std::vector<std::pair<int, double>> micPreset(int mic);

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { if (prepared_) { trim_ = std::pow(10.0, target_[Trim] / 20.0); } }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    int takeParamWrite(int& id, double& plain);
    double gateGainDb() const { return gate_.gainDb(); }
    double compGainDb() const { return compDb_; }
    double deessGainDb() const { return deessDb_; }
    const FeedbackGuard& guard() const { return guard_; }

private:
    void applyParam(int id, double v);
    void updateFilters();
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    double trim_ = 2.0;
    struct Chan { Svf hp, low, mid, high, ds, dsShelf; };
    std::array<Chan, 2> c_{};
    GateEngine gate_;
    GainComputer gc_; LevelDetector det_; Ballistics comp_; double compDb_ = 0, deessDb_ = 0, dsEnv_ = 0, dsA_ = 0, dsR_ = 0, shelfDb_ = 0; unsigned dsTick_ = 0;
    FeedbackGuard guard_;
    std::vector<std::pair<int, double>> writes_;
};

}  // namespace sw::lv03
