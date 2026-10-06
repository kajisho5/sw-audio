// SW DL04 Multitap — six taps on one delay line (spec: 仕様書 v1.0「DL04 Multitap」). Wet signal only (Mix is the shared frame's); reported delay 0.
//   in (L+R mean) + Feedback x loop -> line A (up to 4 s). Per tap n = 1..6: On, Time (1 .. 4000 ms), Level (-60 .. 0 dB), Pan (L100 .. R100, constant power, 0 dB at the edge),
//   Filter (low-pass, 200 Hz .. 20 kHz): out = sum of level x pan x filtered tap. Feedback = the mean of the active taps (filtered, BEFORE Level, so Level only mixes what is heard) x Feedback (0..100 %),
//   which keeps the loop gain <= Feedback. Ping-pong: a second line B with every Pan mirrored; A's loop feeds B and B's feeds A, so each trip round swaps the sides.
//   Filter at 20 kHz = open (bypassed). Sync: each Time reads as ms at 120 bpm and moves to the nearest note length (sw/notes.hpp noteNearest) - so the defaults 250 / 375 / 500 ms are
//   1/8, 1/8 D, 1/4 at every tempo, and the range 1 .. 4000 ms is 1/64 .. 2 bars - then plays at the host tempo, at most 4 s (without a tempo it stays in ms).
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::dl04 {

constexpr int kTaps = 6;
// parameter ids: tap n (0..5) -> kPerTap * n + On .. Filter, then Feedback, Mix, Sync, PingPong
enum TapParam { On, Time, Level, Pan, Filter, kPerTap };
enum GlobalParam { Feedback = kTaps * kPerTap, Mix, Sync, PingPong, kNumParams };
inline constexpr int tapParam(int tap, int p) { return tap * kPerTap + p; }

const std::vector<ParamSpec>& specs();
constexpr double kMaxSeconds = 4.0;

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void setTempo(double bpm) { bpm_ = bpm; }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double tapSeconds(int tap) const;   // the delay in use

private:
    double read(int line, double delay) const;
    void setFilters();
    double fs_ = 48000.0, bpm_ = 0.0, cross_ = 0.0;
    size_t pos_ = 0, mask_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<std::vector<float>, 2> buf_;
    std::array<double, kTaps> delay_{}, on_{}, level_{}, pan_{}, filtHz_{};
    std::array<std::array<Svf, kTaps>, 2> lp_{};
};

}  // namespace sw::dl04
