// SW LV10 Voice Fx — a live voice changer (spec: 仕様書 v1.0「LV10 Voice Fx」). The pitch engine of VO02 (sw::PitchAnalyzer + sw::PsolaSynth, lowest pitch 110 Hz): a fixed ratio 2^(Pitch/12), the vowel moved by Formant.
//   **Reported delay: 1085 samples (22.6 ms at 48 kHz), not the 128 samples of the spec** (the spec itself says to show the real value: a pitch change that follows the voice cannot be as short as one period). The mono sum is processed.
//   Preset (EVO "Anon" among them) writes Pitch, Formant and Robot through takeParamWrite: Low -5 st / -2, High +5 / +2, Robot 0 / 0 / On, Radio 0 / 0 (plus the band-limit below), Anon -3 / +2 (the spec's defaults).
//   Robot On: the pitch is held at 120 Hz x 2^(Pitch/12) whatever is spoken (the ratio is clamped to 0.5 .. 2, so a very high voice is only flattened part of the way). Radio: 300 Hz high-pass, 3.4 kHz low-pass, a soft clip.
//   Anon adds a spectral blur: the formant ratio drifts by +-0.7 st (a smoothed random walk, 0.7 Hz) and the voice goes through 6 first-order all-passes (a dispersive smear of the phase). **This makes a voice harder to recognise; it does not make it anonymous, and
//   a processed voice may be partly undone: do not promise anonymity (spec 要確認).**
//   Mix is the shared frame's (the dry copy is delayed by the latency). Monitor Off: the plugin passes the voice untouched (it is the monitor bus's switch for the performer's own return feed; routing is the host's / the engine's job).
#pragma once
#include "sw/param.hpp"
#include "sw/pitch_engine.hpp"
#include "sw/svf.hpp"
#include <array>
#include <random>
#include <utility>
#include <vector>

namespace sw::lv10 {

enum ParamId { Preset, Pitch, Formant, Robot, Mix, Monitor, kNumParams };
enum PresetId { None = 0, Low = 1, High = 2, RobotP = 3, Radio = 4, Anon = 5 };

const std::vector<ParamSpec>& specs();
PitchConfig engineConfig(double fs);
std::array<std::pair<int, double>, 3> presetValues(int preset);   // (Pitch, Formant, Robot) written by a preset; a preset that writes nothing gives ids of -1 (a fixed array: it is called on the audio thread)

class Processor : public RatioSource {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const;
    int takeParamWrite(int& id, double& plain);
    void ratio(double f0, bool voiced, double dt, double& r, double& fm) override;
    bool monitorOn() const { return target_[Monitor] > 0.5; }

private:
    void applyParam(int id, double v);
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    PitchAnalyzer an_;
    PsolaSynth synth_;
    Svf hp_, lp_;
    std::array<double, 6> apz_{};
    double jitter_ = 0, jitterTarget_ = 0, jitterClock_ = 0;
    std::mt19937 rng_{12345};
    std::vector<std::pair<int, double>> writes_;
};

}  // namespace sw::lv10
