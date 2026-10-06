// SW MD04 Tremolo Pan — tremolo, auto-pan and harmonic tremolo (spec: 仕様書 v1.0「MD04 Tremolo Pan」). No Mix (the whole signal is processed); reported delay 0.
//   LFO u in -1 .. 1: Sine, Triangle, Square or Ramp (Square and Ramp edges are smoothed with a 2 ms time constant against clicks). uni = (u + 1) / 2.
//   Tremolo: gain = 1 - Depth (1 - uni) (Depth 100 % goes down to silence); Width = the phase of the right channel's LFO against the left's, 0 .. 180 degrees.
//   Auto pan: theta = pi/4 (1 + Depth Width u), left x sqrt2 cos theta, right x sqrt2 sin theta (1 at the centre, constant power, a stereo input is balanced).
//   Harmonic: split at 800 Hz (Linkwitz-Riley 4th order), low gain = 1 - Depth (1 - uni), high gain = 1 - Depth uni (opposite phases: they add up to 2 - Depth); Width as in Tremolo.
//   Rate 0.1 .. 20 Hz; Sync: Rate read as the cycle length at 120 bpm moved to the nearest note and played at the host tempo (the default 4 Hz = 1/8); with the host playing the phase is put on the bar grid.
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::md04 {

enum ParamId { Mode, Rate, Depth, Shape, Width, Sync, kNumParams };
enum ModeId { Tremolo = 0, AutoPan = 1, Harmonic = 2 };
enum ShapeId { Sine = 0, Triangle = 1, Square = 2, Ramp = 3 };

const std::vector<ParamSpec>& specs();
double lfoValue(int shape, double phase);   // -1 .. 1 (before the edge smoothing)

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
    double rateHz() const;
    double lfo(int ch) const { return lastLfo_[static_cast<size_t>(ch)]; }

private:
    double fs_ = 48000.0, bpm_ = 0.0, ph_ = 0.0, slew_[2] = {0, 0}, lastLfo_[2] = {0, 0};
    bool prepared_ = false, syncPending_ = false;
    double syncBeats_ = 0.0;
    std::array<double, kNumParams> target_{};
    struct Split { Svf lp1, lp2, hp1, hp2; };
    std::array<Split, 2> split_{};
};

}  // namespace sw::md04
