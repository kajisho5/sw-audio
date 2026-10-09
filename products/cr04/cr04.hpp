// SW CR04 Freeze — holds the spectrum of the moment and keeps playing it (spec: 仕様書 v1.0「CR04 Freeze」). The dry signal is never delayed: reported delay 0. Mix is done here:
//   out = dry x (1 - Mix x a) + frozen x Mix x a, a = 0 .. 1 follows the freeze (attack 20 ms; release 400 ms for Hold, 40 ms for Momentary).
//   Capture: the last 4096 samples (Hann) -> magnitudes; the peaks (local maxima over 1 % of the largest) are sinusoids: every bin takes the phase of its nearest peak with the Hann window's alternating sign
//   (-1)^(k - peak); the peak's frequency comes from the parabolic interpolation, its phase moves by omega x hop (1024) per frame, plus a random step for Drift (Off 0, Slow 0.1 rad, Fast 0.6 rad per frame, and Blur / 100 x 0.2
//   more). Frames of 4096, Hann synthesis window, hop 1024 (overlap 4, sum 2): a captured sine keeps its level and its phase. Blur 0 .. 100 %: the magnitudes are averaged over +-0 .. 24 bins.
//   Trigger: Hold / Momentary = the capture is made when Freeze goes On (the texture lasts while it is On); Auto = Freeze On arms it and every onset of the input (a 9 dB rise of the 5 ms level over the 100 ms level,
//   over -50 dB, 150 ms apart) makes a new capture (a 15 ms cross-fade). MIDI (spec: "MIDI notes can capture too"): while Freeze is On a note-on makes a new capture in every trigger mode,
//   the same way an onset does in Auto (a 15 ms cross-fade).
#pragma once
#include "sw/fft.hpp"
#include "sw/param.hpp"
#include <array>
#include <cstdint>
#include <complex>
#include <vector>

namespace sw::cr04 {

enum ParamId { Trigger, Freeze, Blur, Drift, Mix, kNumParams };
enum TriggerId { Hold = 0, Momentary = 1, AutoTrig = 2 };

const std::vector<ParamSpec>& specs();
constexpr int kN = 4096, kHop = 1024;

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    bool frozen() const { return a_ > 0.001; }
    int captures() const { return captures_; }
    // MIDI (audio thread): a note-on while Freeze is On asks for a new capture
    void noteOn() { if (target_[Freeze] > 0.5) pending_ = true; }

private:
    struct Chan {
        bool have = false;
        std::vector<double> mag, magBlur, omega, phi;   // per bin; omega / phi are per peak (indexed by the peak's bin)
        std::vector<int> peakOf;
        std::vector<float> ola;
    };
    void capture(int64_t t0);
    void frameFor(Chan& c, int64_t start, bool advance);
    double gauss();
    double fs_ = 48000.0, a_ = 0.0, fade_ = 1.0, env_ = 0.0, slow_ = 0.0;
    int64_t t_ = 0, nextFrame_ = 0, lastOnset_ = -1000000;
    int captures_ = 0, blurDone_ = -1;
    bool prepared_ = false, wantOn_ = false, pending_ = false;
    uint32_t rng_ = 0x1234567u;
    std::array<double, kNumParams> target_{};
    std::array<std::vector<float>, 2> ring_;
    std::array<Chan, 2> ch_;
    Fft fft_;
    std::vector<std::complex<double>> work_;
    std::vector<double> win_;
    std::vector<int> peaksTmp_;
    size_t wpos_ = 0, mask_ = 0;
};

}  // namespace sw::cr04
