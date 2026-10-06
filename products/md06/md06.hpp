// SW MD06 Freq Shift — frequency shifter (spec: 仕様書 v1.0「MD06 Freq Shift」). Wet signal only (Mix is the shared frame's); reported delay 0.
//   An IIR Hilbert pair (sw::HilbertIir) gives i and q (q leads 90 degrees); out = i cos(w t) +- q sin(w t) (+ = up) shifts every partial by +-s Hz (not a pitch shift: harmonics stop being harmonic).
//   Shift is symmetric-logarithmic (-2000 .. +2000 Hz, 0 in the middle, +35 Hz default). Direction: Up uses |Shift| (up), Down uses -|Shift| (down), Both follows the sign of Shift.
//   Ring mod: out = i cos(w t) (both sidebands). Feedback (0 .. 100 %) takes the output back to the input through a soft limiter (a barber-pole spiral of copies, each one s further).
//   LFO (On): the shift is multiplied by sin(2 pi x 0.25 Hz t): it sweeps from +s through 0 to -s and back (design). Pitch track (md06.evo.on, class B): the shift is multiplied by f0 / 220 Hz
//   (pitch tracker, 0.15 s glide, holds the last pitch), so the displayed value is the shift at A3 and the same interval in cents stays at other pitches.
#pragma once
#include "sw/hilbert.hpp"
#include "sw/param.hpp"
#include "sw/pitch_tracker.hpp"
#include <array>
#include <vector>

namespace sw::md06 {

enum ParamId { Shift, Direction, RingMod, Feedback, Lfo, Mix, PitchTrack, kNumParams };
enum DirectionId { Up = 0, Down = 1, Both = 2 };

const std::vector<ParamSpec>& specs();
constexpr double kRefHz = 220.0, kLfoHz = 0.25;

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double shiftHz() const { return lastShift_; }   // the shift in use at the last sample

private:
    double fs_ = 48000.0, osc_ = 0.0, lfoPh_ = 0.0, lastShift_ = 0.0, f0Smooth_ = 0.0, fbState_[2] = {0, 0};
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<HilbertIir, 2> hil_{};
    PitchTracker pitch_;
};

}  // namespace sw::md06
