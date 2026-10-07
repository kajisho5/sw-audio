// SW CR06 One Knob — six effects on one Amount knob (spec: 仕様書 v1.0「CR06 One Knob」). Mix and Output are the shared frame's; reported delay 0. Amount 0 is the dry signal for every effect.
//   The inner chains are simplified versions of the bundle's products (the spec: "a combination of existing products"), each moved by Amount t = 0 .. 1 along its own curve:
//   Wide: mid/side, side x (1 + 2t) and +3t dB above 4 kHz (SA/ST width). Warm: tanh drive 1 + 3t blended in by t, low shelf +3t dB at 150 Hz, high shelf -3t dB at 6 kHz (SA01 + EQ04 style).
//   Air: high shelf +6t dB at 10 kHz and an exciter (the band above 6 kHz through tanh(3x), added x 0.1t) (SA05 + the EQ01 air band). Punch: a transient emphasis, gain 1 + 1.5t x the rise of a 2 ms
//   envelope over a 40 ms one (up to +8 dB on the attack) with the sustain pulled down by up to 2t dB (DY07 + DY09). Space: the plate reverb core (RV02: 1.2 s, pre-delay 15 ms, damping 60 %, low cut 150 Hz)
//   added x 0.5t. Lo-fi: sample-rate reduction by 1 + 11t (hold, no filter), quantisation from 16 to 6 bits (6 + 10 (1 - t)), a low-pass from 12 kHz down to 3 kHz.
//   Macro (cr06.macro, UI only): macroValue(i) tells the screen the inner values of the chosen effect (i = 0 .. 2).
#pragma once
#include "rv02/rv02.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::cr06 {

enum ParamId { Effect, Amount, Mix, Output, Macro, kNumParams };
enum EffectId { Wide = 0, Warm = 1, Air = 2, Punch = 3, Space = 4, Lofi = 5 };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { if (prepared_) setFilters(); }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double macroValue(int i) const;

private:
    void setFilters();
    double fs_ = 48000.0;
    int maxBlock_ = 512, lastEffect_ = -1;
    double lastAmount_ = -1.0, fastE_ = 0.0, slowE_ = 0.0, hold_[2] = {0, 0};
    int holdCount_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    struct Chan { Svf a, b, c; };
    std::array<Chan, 2> ch_{};
    rv02::Processor plate_;
    std::array<std::vector<float>, 2> send_;
};

}  // namespace sw::cr06
