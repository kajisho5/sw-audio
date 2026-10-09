// SW DL05 Reverse — reverse / forward / random grain delay (spec: 仕様書 v1.0「DL05 Reverse」). Wet signal only (Mix is the shared frame's); reported delay 0 (the delay is the effect).
//   Time P = a note length (1/16 .. 2 bars, at the host tempo, 120 bpm without one; at most 4 s). The input is recorded into a 10 s ring. The wet signal is made of Hann grains
//   (length 2 hop = Grain size, a new one every hop, so the windows add up to exactly 1). A grain reads the ring from s0 at speed dir x rate, rate 2 with Pitch +12:
//     Reverse: the segment (u = time since the last segment boundary, boundaries every P) is played backwards: s0 = now - (1 + rate) u, dir = -1  => wet(t) = in(2 B - t), B the boundary before t.
//     Forward: s0 = now - P + (rate - 1) (u mod P / (2 (rate - 1))), dir = +1 (a plain delay at rate 1). Random: s0 = now - r P (r random), dir = +-1.   Spray moves every grain's s0 by +-Spray x grain length.
//   (s0 moves by rate x hop from one grain to the next, so overlapping grains stay in phase.)
//   Segment boundaries run free from the start; with the host playing and the bar line known they are put on the grid (next bar line - k P), so a reverse starts on the beat (setTransport).
//   Freeze stops the recording at tf and wraps every read into [tf - P, tf): the last P seconds go round (reversed in Reverse mode).
#pragma once
#include "sw/param.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace sw::dl05 {

enum ParamId { Mode, Time, GrainSize, Spray, Pitch, Freeze, Mix, kNumParams };
enum ModeId { Reverse = 0, Forward = 1, Random = 2 };
constexpr int kNumTimes = 13;      // 1/16 .. 2 bars: notes 5 .. 17 of sw/notes.hpp
constexpr double kMaxSeconds = 4.0;

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void setTempo(double bpm) { bpm_ = bpm; }
    void setTransport(bool playing, double beatsToNextBar);
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double timeSamples() const;   // P in use
    // for the screen (audio thread): per grain slot (kGrainSlots) 5 values: on (0 / 1), how far back in the recording it reads now (seconds), its speed (direction x rate: -2 .. 2),
    // where it is in its window (0 .. 1) and how much source it spans (seconds, length x rate). `frozen()`: the recording is stopped.
    static constexpr int kGrainSlots = 4, kGrainValues = 5;
    void grains(double* out) const;
    bool frozen() const { return frozen_; }
    double sampleRate() const { return fs_; }

private:
    struct Grain { bool on = false; double s0 = 0, dir = 1, rate = 1, tau = 0, len = 2; };
    double read(int c, double src) const;
    double rnd() { rng_ = rng_ * 1664525u + 1013904223u; return (rng_ >> 8) * (1.0 / 16777216.0); }
    void spawn();
    double fs_ = 48000.0, bpm_ = 0.0, u_ = 0.0, tf_ = 0.0;
    int64_t w_ = 0, validFrom_ = 0;
    size_t mask_ = 0;
    int hopLeft_ = 0;
    bool prepared_ = false, frozen_ = false;
    bool syncPending_ = false; double syncBeats_ = 0.0;
    unsigned rng_ = 0x2545f491u;
    std::array<double, kNumParams> target_{};
    std::array<std::vector<float>, 2> buf_;
    std::array<Grain, 4> grain_{};
};

}  // namespace sw::dl05
