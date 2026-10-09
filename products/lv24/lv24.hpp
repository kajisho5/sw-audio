// SW LV24 Live Reverb — a light reverb for live voice (spec: 仕様書 v1.0「LV24 Live Reverb」). Wet signal only (Mix is the shared frame's, default 18 %); no reported delay.
//   Mono sum -> Pre-delay (0..200 ms, Skew k = 2) -> an 8-line FDN (sw::Fdn: Householder matrix, modulated lines) -> two decorrelated outputs, scaled to unit energy for the Decay as in RV01.
//   Type sets the line lengths (a set of coprime lengths scaled by a factor: Vocal hall x1 with a mean of 58 ms, Room x0.45, Plate x0.6) and the density; Tone sets the damping of the lines (Warm 3.5 kHz, Neutral 6.5 kHz, Bright 10 kHz).
//   Duck (EVO, class A): while sw::VoiceDetector hears speech the wet level goes down by 9 dB (80 ms attack, 500 ms release): the voice stays clear and the reverb blooms in the pauses.
//   **No CPU figure is claimed**: the spec's "1 % CPU" is an unmeasured number (its own 要確認): the engine here is 8 lines and a few filters, and the screen should say "Low CPU".
#pragma once
#include "sw/fdn.hpp"
#include "sw/param.hpp"
#include "sw/voice_detect.hpp"
#include <array>
#include <vector>

namespace sw::lv24 {

enum ParamId { Type, Decay, PreDelay, Tone, Mix, Duck, kNumParams };
enum TypeId { VocalHall = 0, Room = 1, Plate = 2 };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { if (prepared_) apply(true); }
    void process(float** ch, int numCh, int n);
    double tailSeconds() const;   // how long it goes on after the input stops (sw/tail.hpp)
    int latencySamples() const { return 0; }
    double duckGainDb() const { return 20.0 * std::log10(std::max(duck_, 1e-9)); }

private:
    void apply(bool snap);
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    Fdn fdn_;
    VoiceDetector vd_;
    std::vector<float> pre_, mono_;
    size_t preMask_ = 0, prePos_ = 0;
    double preLen_ = 0, trim_ = 1.0, duck_ = 1.0;
};

}  // namespace sw::lv24
