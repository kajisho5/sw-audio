// SW MD07 Ensemble — string-ensemble multi-voice chorus (spec: 仕様書 v1.0「MD07 Ensemble」). Wet signal only (Mix is the shared frame's); reported delay 0.
//   N voices (2 / 3 / 4 / 6), each a tap of a delay line (centre 10 ms) moved by two LFOs: a slow one (Rate: 0.15 .. 3 Hz, 0.15 x 20^(Rate/10)) of +-Depth/10 x 4 ms and a fast one
//   (12 x the slow rate) a quarter of that (design). The voices' phases are spaced evenly round the circle (slow: v/N, fast: -v/N of a cycle): the first-order delay changes of all voices add up to zero,
//   so the sum is mono compatible (the MD01 construction, applied to every voice). Spread (0..10) puts the voices from the centre (0) to the whole stereo width (10) with a LINEAR pan (left
//   1 - p, right p: the two gains always add up to 1, which keeps the cancellation in L + R); the output is normalised to unit power. Tone: low-pass of the wet 2.5 .. 14 kHz (2nd order) and a 120 Hz high-pass.
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::md07 {

enum ParamId { Voices, Spread, Rate, Depth, Tone, Mix, kNumParams };

const std::vector<ParamSpec>& specs();
constexpr int kMaxVoices = 6;
double slowHz(double rate);          // Rate 0..10 -> 0.15 .. 3 Hz
double voicePan(int voice, int voices, double spread);   // 0 (left) .. 1 (right)

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double voiceDelaySamples(int voice) const { return lastDelay_[static_cast<size_t>(voice)]; }

private:
    double read(int c, double delay) const;
    void setTone();
    double fs_ = 48000.0, slowPh_ = 0.0, fastPh_ = 0.0, toneState_ = -1.0, lastDelay_[kMaxVoices] = {0, 0, 0, 0, 0, 0};
    size_t pos_ = 0, mask_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<std::vector<float>, 2> buf_;
    struct Chan { Svf lp, hp; };
    std::array<Chan, 2> ch_{};
};

}  // namespace sw::md07
