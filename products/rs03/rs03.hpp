// SW RS03 Dehum — a comb of notches at the mains hum and its harmonics (spec: 仕様書 v1.0「RS03 Dehum」). Reported delay 0.
//   Base 50 / 60 Hz / Auto; Harmonics 2 / 4 / 8 / 16 = how many notches (the fundamental and the multiples up to that number); Depth 0 .. 10 = 0 .. -40 dB (4 dB a step);
//   Width Narrow .. Wide (0 .. 100 %): Q 60 .. 8 (log; the bandwidth at the half-depth point: 0.8 .. 6 Hz at 50 Hz). Each notch is a peaking filter with negative gain (sw::Svf bell).
//   Buzz 0 .. 10 (design): the odd multiples are cut Buzz x 1 dB deeper, and notches are added for the multiples Harmonics+1 .. 2 x Harmonics with Buzz x 4 dB (up to -40 dB).
//   Track (EVO, On): a frequency estimate follows the hum within +-2 Hz of the base: the first four multiples are projected on 17 frequencies spaced 0.25 Hz over 1 s windows of a signal
//   low-passed and decimated to 1.5 kHz; the peak (parabolic interpolation, only when it stands 2x over the median) moves the estimate by half the step; all notches move with it.
//   Auto looks around 50 and 60 Hz (both searched) and takes the one with the stronger peak twice in a row. Off: the notches stay at the base.
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <algorithm>
#include <array>
#include <complex>
#include <vector>

namespace sw::rs03 {

enum ParamId { Base, Harmonics, Depth, Width, Buzz, Track, kNumParams };
enum BaseId { Hz50 = 0, Hz60 = 1, Auto = 2 };

const std::vector<ParamSpec>& specs();
constexpr int kMaxNotches = 32;

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { if (prepared_) setFilters(0); }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    // any notch count 1 .. 16 (LV09 Hum Cut); the Harmonics parameter itself only takes 2 / 4 / 8 / 16
    void setHarmonicCount(int h) { target_[Harmonics] = std::clamp(h, 1, 16); }
    double humHz() const { return f0_; }   // the frequency in use (the display)

private:
    static constexpr int kCand = 17, kHarmTrack = 4, kBlock = 128;
    void setFilters(int ramp);
    void trackerPush(double x);
    void trackerEvaluate();
    double effectiveBase() const;
    double fs_ = 48000.0, f0_ = 50.0, lp1_ = 0.0, lp2_ = 0.0, lpC_ = 0.0, fd_ = 1500.0, lastF0_ = -1.0, lastKey_ = -1.0;
    int dec_ = 32, decCount_ = 0, winLen_ = 1500, winPos_ = 0, autoGroup_ = 0, votePick_ = -1, votes_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    struct Chan { std::array<Svf, kMaxNotches> notch; };
    std::array<Chan, 2> ch_{};
    std::array<bool, kMaxNotches> active_{};
    std::vector<std::complex<double>> acc_;   // [2 groups (50 / 60 Hz)] x kCand x kHarmTrack
    std::vector<double> power_;
};

}  // namespace sw::rs03
