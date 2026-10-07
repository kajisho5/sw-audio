// SW LV21 Test Gen — a test signal generator (spec: 仕様書 v1.0「LV21 Test Gen」). Reported delay 0; no Auto gain, no Delta.
//   Signals: Sine (Freq), Pink (Paul Kellet's filter, scaled to the Level as RMS), White (RMS), Sweep (a logarithmic sweep 20 Hz .. 20 kHz over Sweep time, repeating; Level as the RMS of a sine), Polarity (a positive-going pulse
//   of 0.1 ms twice a second at the Level as peak, for LV22 and for checking the polarity of a speaker). Left / Right switch the channels (the other keeps the input).
//   Output is two steps (not parameters; "Auto 不可"): arm() then outputOn(). Until both, the input passes untouched; while on, the generator REPLACES the input on the channels that are On. At load and after a session restore it is always off
//   (nothing about it is saved: the parameters only hold the signal's settings), the level comes up over 0.5 s, and it stops by itself after 60 s (the spec's proposal: kAutoStopSeconds). disarm() / outputOff() stop at once with a 20 ms fade.
#pragma once
#include "sw/param.hpp"
#include <array>
#include <random>
#include <vector>

namespace sw::lv21 {

enum ParamId { Signal, Freq, Level, SweepTime, Left, Right, kNumParams };
enum SignalId { Sine = 0, Pink = 1, White = 2, Sweep = 3, PolarityPulse = 4 };
constexpr double kAutoStopSeconds = 60.0;

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    void arm() { armed_ = true; }
    void disarm() { armed_ = false; on_ = false; }
    bool outputOn();      // true when it started (it needs to be armed)
    void outputOff() { on_ = false; }
    bool armed() const { return armed_; }
    bool running() const { return on_; }
    double elapsed() const { return elapsed_; }

private:
    double next(int signal);
    double fs_ = 48000.0, phase_ = 0.0, sweepPhase_ = 0.0, sweepT_ = 0.0, env_ = 0.0, elapsed_ = 0.0, pulseClock_ = 0.0;
    bool prepared_ = false, armed_ = false, on_ = false;
    std::array<double, kNumParams> target_{};
    std::array<double, 7> pink_{};
    std::mt19937 rng_{1};
};

}  // namespace sw::lv21
