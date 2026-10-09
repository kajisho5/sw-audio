// SW LV07 Speech Agc — a talker leveller (spec: 仕様書 v1.0「LV07 Speech Agc」). Reported delay 0.
//   Level: the 400 ms K-weighted loudness of the input (sw::LoudnessMeter::momentary) while the signal is over Gate (a 100 ms RMS in dBFS); the gain moves towards (Target - loudness), up to Max gain, down to -12 dB,
//   at a rate set by Speed (up / down dB per second: Slow 2 / 6, Medium 6 / 12, Fast 15 / 30) times a Use factor (Speech 1, Panel 1.5 — several talkers, quick changes, Lecture 0.6 — one steady talker).
//   Under the Gate the level is not measured: Talker hold On keeps the gain; Off lets it drift back to 0 dB at 3 dB/s. Freeze keeps the gain as it is. A peak limiter at -1 dBFS (instantaneous, 80 ms) follows the gain.
//   EVO (class B, rules): near / far. While speech runs, frames of 20 ms are kept for 3 s: the spread of their levels (loud 90 % minus quiet 10 %; a near talker has gaps, a far one is filled with room sound) and the
//   high / mid ratio (energy over 3 kHz against a band around 1 kHz in the loud frames, 12 dB under the peak frame at most: a far talker loses highs; 0 at -26 dB, 1 at -38 dB — calibrated on synthetic voices only). distance() is 0 (near) .. 1 (far). Far talkers get a presence shelf (+4 dB x distance at 3 kHz) and 3 dB x distance more Max gain.
#pragma once
#include "sw/loudness.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::lv07 {

enum ParamId { Use, Target, MaxGain, Speed, Gate, TalkerHold, Freeze, kNumParams };
enum UseId { Speech = 0, Panel = 1, Lecture = 2 };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double gainDb() const { return gDb_; }
    double distance() const { return dist_; }
    double hiMidDb() const { return hiMidN_ ? hiMid_ / hiMidN_ : 0.0; }
    double loudnessLufs() const { return meter_.momentary(); }

private:
    static constexpr int kRun = 32;   // the control grid of the stream: the gate level, near / far and the gain are decided every 32 samples, wherever the host's block starts
    void part(float** ch, bool stereo, int n);
    void controlBlock();
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    LoudnessMeter meter_;
    double gDb_ = 0, lim_ = 1, rms_ = 0, dist_ = 0, shelfDb_ = 0, seen_ = 0, blockPow_ = 0, gA_ = 1.0, gB_ = 1.0;   // gA_ -> gB_: the gain ramp over the control block
    int ph_ = 0;                                                                                                       // samples into the control block
    // near / far
    std::array<Svf, 2> hi_{}, mid_{}, pres_{};
    double eHi_ = 0, eMid_ = 0, frameE_ = 0; int frameN_ = 0, frameLen_ = 960, nFrames_ = 0, head_ = 0;
    std::vector<float> frames_;
    double hiMid_ = 0, frameMax_ = -120; int hiMidN_ = 0;
    unsigned tick_ = 0;
    std::vector<float> scratch_;
};

}  // namespace sw::lv07
