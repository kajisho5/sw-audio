// SW MD02 Flanger — modulated-delay flanger with Through zero (spec: 仕様書 v1.0「MD02 Flanger」). Wet signal only (Mix is the shared frame's).
//   wet = a delay line read at d(t), fed by in + Feedback x limit(wet) (L tanh(x / L), L = 4: gain 1 for small signals). The right channel's LFO runs 90 degrees behind the left's (design).
//   Without Through zero: d = Manual x 2^(1.5 Depth sin) (centre Manual; at Depth 100 % the delay spans Manual / 2.83 .. 2.83 Manual); latency 0.
//   Through zero (md02.evo.on, default On): the frame delays the dry signal by 10 ms (the reported latency, 480 samples at 48 kHz) and d = 10 ms + Manual x Depth x sin: the swept delay
//   goes from after the dry signal to before it and back, crossing it (a delay shorter than the dry's own: the wet leads). The latency is the one fixed at prepare().
//   Sync: the LFO period is a note length (Rate, read as the period at 120 bpm, moves to the nearest note, played at the host tempo); with the host playing and the bar line known
//   the LFO phase is put on the grid next bar line - k periods (setTransport), so the sweep repeats in time with the song.
#pragma once
#include "sw/param.hpp"
#include <array>
#include <vector>

namespace sw::md02 {

enum ParamId { Rate, Depth, Feedback, Manual, ThroughZero, Sync, Mix, kNumParams };

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
    int latencySamples() const;                 // the value for the next prepare
    double rateHz() const;                      // the sweep rate in use (Sync: from the note length)
    double delaySamples(int ch) const { return lastDelay_[static_cast<size_t>(ch)]; }
    double lfoPhase() const { return ph_; }

private:
    double read(int c, double delay) const;
    double fs_ = 48000.0, bpm_ = 0.0, ph_ = 0.0, lastDelay_[2] = {0, 0}, fbState_[2] = {0, 0};
    size_t pos_ = 0, mask_ = 0;
    int latency_ = 0;
    bool prepared_ = false, syncPending_ = false;
    double syncBeats_ = 0.0;
    std::array<double, kNumParams> target_{};
    std::array<std::vector<float>, 2> buf_;
};

}  // namespace sw::md02
