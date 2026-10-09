// SW DL02 Tape Echo — three-head tape echo (spec: 仕様書 v1.0「DL02 Tape Echo」). Wet signal only (Mix is the shared frame's); reported delay 0.
//   One tape loop: record = tone(sat(in + feedback)) -> delay line; the heads read it at 1 t, 2 t and 3 t (t = Rate, 50 .. 200 ms; Slow = 200 ms on the left of the knob).
//   Heads picks which are heard (1, 2, 3, 1+2, 2+3, All); out = sum / sqrt(number of heads), the feedback is their mean x Intensity (0 .. 10 = 0 .. 110 %).
//   Tape speed ties the time and the bandwidth: the head-gap low-pass is 9 kHz x sqrt(100 ms / t), then x (1 - 0.6 Wear / 10). Saturation 1.0 x tanh(x / 1.0) (small signals unchanged).
//   Wear (0 .. 10): wow (0.12 ms per step, 0.55 / 1.37 Hz) and flutter (0.006 ms per step, 9.1 Hz) on the loop speed (the heads' delays scale with their distance), the high-frequency loss,
//   and dropouts (random 20..60 ms dips of 4..10 dB, (Wear - 2) x 0.375 per second above Wear 2). Bass / Treble: shelves at 200 Hz / 3 kHz (+-6 dB) inside the loop.
//   A change of Heads fades 10 ms; with the host playing and the bar position known it waits for the next bar line (setTransport).
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::dl02 {

enum ParamId { Heads, Rate, Intensity, Bass, Treble, Wear, Mix, Unit, kNumParams };
enum HeadsId { H1 = 0, H2 = 1, H3 = 2, H12 = 3, H23 = 4, All = 5 };

const std::vector<ParamSpec>& specs();
int headMask(int heads);   // bit k = head k + 1 is heard

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void setTempo(double bpm) { bpm_ = bpm; }
    void setTransport(bool playing, double beatsToNextBar);
    void process(float** ch, int numCh, int n);
    double tailSeconds() const;   // how long the repeats go on (sw/tail.hpp)
    int latencySamples() const { return 0; }

private:
    double read(int c, double delay) const;
    void setTone();
    double fs_ = 48000.0, bpm_ = 0.0, t_ = 0.0, countdown_ = 0.0, dropGain_ = 1.0, dropTarget_ = 1.0, ph_[3] = {0, 0, 0};
    long dropLeft_ = 0;
    size_t pos_ = 0, mask_ = 0;
    int applied_ = 3;
    bool prepared_ = false, barKnown_ = false;
    unsigned rng_ = 0x1badf00du;
    std::array<double, kNumParams> target_{};
    std::array<double, 3> g_{};
    std::array<std::vector<float>, 2> buf_;
    struct Chan { Svf lp, bass, treble; };
    std::array<Chan, 2> ch_{};
    double toneRate_ = -1, toneWear_ = -1, toneBass_ = -99, toneTreble_ = -99;
    unsigned rnd() { rng_ = rng_ * 1664525u + 1013904223u; return rng_; }
};

}  // namespace sw::dl02
