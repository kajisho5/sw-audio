// SW MD03 Phaser — chain of first-order all-passes swept in frequency (spec: 仕様書 v1.0「MD03 Phaser」). Wet signal only (Mix is the shared frame's); reported delay 0.
//   N stages (4 / 6 / 8 / 12), all at fc(t) = Center x 2^(2 Depth / 10 x sin(phase)) (Depth 10 = +-2 octaves), a = (t - 1) / (t + 1), t = tan(pi fc / fs): each stage is -90 degrees at fc, so
//   dry + wet has N/2 notches, the first at fc x tan(pi / 2N). Feedback (0..10 = 0 .. 0.9) takes the chain's last output back to its input (|H| = 1, so the loop is stable). The right channel
//   sweeps 90 degrees behind the left. The wet is the all-pass output itself: with Mix 50 % the notches are full.
//   Sync: the sweep period is a note length (Rate read as the period at 120 bpm, moved to the nearest note, played at the host tempo; the phase is put on the bar grid while playing).
//   Note follow (md03.evo.on): the pitch tracker (70 .. 1000 Hz) moves Center to Center x f0 / 220 Hz (glides over 0.15 s; holds the last pitch while unvoiced), so every note has its notches at the same harmonics.
#pragma once
#include "sw/param.hpp"
#include "sw/pitch_tracker.hpp"
#include <array>
#include <vector>

namespace sw::md03 {

enum ParamId { Stages, Rate, Depth, Feedback, Center, Mix, Sync, NoteFollow, kNumParams };

const std::vector<ParamSpec>& specs();
constexpr double kRefHz = 220.0;

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
    double sweepHz(int ch) const { return lastFc_[static_cast<size_t>(ch)]; }     // the all-pass frequency at the last sample
    double centreHz() const { return centreEff_; }                                // Center after Note follow

private:
    double fs_ = 48000.0, bpm_ = 0.0, ph_ = 0.0, centreEff_ = 800.0, lastFc_[2] = {800, 800}, fbState_[2] = {0, 0}, f0Smooth_ = 0.0;
    bool prepared_ = false, syncPending_ = false;
    double syncBeats_ = 0.0;
    std::array<double, kNumParams> target_{};
    std::array<std::array<double, 12>, 2> z_{};
    PitchTracker pitch_;
};

}  // namespace sw::md03
