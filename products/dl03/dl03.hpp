// SW DL03 Bbd — bucket-brigade analog delay (spec: 仕様書 v1.0「DL03 Bbd」). Wet signal only (Mix is the shared frame's); reported delay 0.
//   in + Feedback x echo -> tanh -> compressor (2:1 compander) -> clock-following low-pass -> BUCKET: the signal is sampled at the clock f = min(4096 / Time, fs)
//   (a tick every fs / f samples) into a ring of ticks, read 4096 ticks (= Time) behind and held between ticks -> low-pass -> expander -> echo.
//   Longer Time = slower clock = narrower band and more aliasing (the pre-filter is one 2nd-order low-pass at 0.4 f, so tones above f / 2 fold back; the post-filter two at 0.2 f; f is the unclamped 4096 / Time). Compander: x_c = x / sqrt(E(x) + 0.003), y = y_c E(y_c)
//   (E = rectified 5 ms follower): unity overall, and it hides / breathes with the BBD's noise. Mod depth / rate move the delay (and so the clock); Grit adds the
//   BBD's fizz in the bucket (after the compressor, so the expander shapes it): it only ever touches the echoes (the dry sound is the frame's).
//   Sync: Time (ms) is moved to the nearest note length at the host tempo (log distance), kept within 20 .. 600 ms.
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::dl03 {

enum ParamId { Time, Feedback, ModDepth, ModRate, Grit, Mix, Sync, Unit, kNumParams };

const std::vector<ParamSpec>& specs();
constexpr int kStages = 4096;

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void setTempo(double bpm) { bpm_ = bpm; }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double timeSeconds() const;      // the delay in use
    double clockHz() const;          // the bucket clock for the delay in use

private:
    struct Chan {
        std::vector<float> ring; size_t pos = 0; double acc = 0.0, held = 0.0, env1 = 0.0, env2 = 0.0; Svf pre1, post1, post2; unsigned rng = 1;
    };
    void setFilters(double clock);
    double fs_ = 48000.0, bpm_ = 0.0, t_ = 0.0, ph_ = 0.0, fbState_[2] = {0, 0};
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<Chan, 2> ch_{};
    double lastCut_ = -1.0;
};

}  // namespace sw::dl03
