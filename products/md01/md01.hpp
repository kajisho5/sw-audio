// SW MD01 Chorus — BBD-style chorus (spec: 仕様書 v1.0「MD01 Chorus」). Wet signal only (Mix is the shared frame's); reported delay 0.
//   A modulated delay (centre 7 ms) per channel: tap = 7 ms + dev x triangle(phase), dev = Depth / 10 x 3 ms x (I 0.6, II 1.0). Mode I: one voice at Rate; II: one voice at 1.6 x Rate;
//   I+II: both voices (the second 90 degrees later in its phase), each at 1/sqrt(2). Width (Mono .. Wide) sets the phase of the right channel's LFO against the left's: 0 .. 180 degrees.
//   At Wide the two channels move in opposite directions, so in L+R the first-order wobble cancels: it does not turn muddy in mono. Tone: low-pass of the wet 2.5 .. 14 kHz (BBD band limit,
//   2nd order) plus a high-pass at 120 Hz; a little BBD hiss (-78 dBFS) that is there only while there is signal.
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::md01 {

enum ParamId { Mode, Rate, Depth, Width, Tone, Mix, kNumParams };
enum ModeId { I = 0, II = 1, I_II = 2 };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double tapDelaySamples(int ch, int voice) const { return lastDelay_[static_cast<size_t>(ch)][static_cast<size_t>(voice)]; }   // for the tests: the delay in use at the last sample

private:
    double read(int c, double delay) const;
    void setTone();
    double fs_ = 48000.0, ph_ = 0.0, ph2_ = 0.0, env_ = 0.0, toneState_ = -1.0;
    size_t pos_ = 0, mask_ = 0;
    bool prepared_ = false;
    unsigned rng_ = 0x1234567u;
    std::array<double, kNumParams> target_{};
    std::array<std::vector<float>, 2> buf_;
    struct Chan { Svf lp, hp; };
    std::array<Chan, 2> ch_{};
    double lastDelay_[2][2] = {{0, 0}, {0, 0}};
};

}  // namespace sw::md01
