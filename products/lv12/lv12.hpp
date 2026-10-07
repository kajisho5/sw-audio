// SW LV12 Geq 31 — a 31-band graphic EQ (spec: 仕様書 v1.0「LV12 Geq 31」). Reported delay 0. Constant-Q bells (Q 4.3) at the ISO centres 20 Hz .. 20 kHz, +-12 dB, on both channels; HPF (Off, 20..200 Hz, 2nd order) and LPF (5..20 kHz, Off at the top).
//   Left / right: the parameters "Band 20 Hz" .. "Band 20 kHz" are the left channel (and both while Link L/R is On); with Link Off the right channel takes its own 31 gains, the parameters "R 20 Hz" .. "R 20 kHz"
//   appended after the others (design: the spec's table has 31 gains and an Edit switch; two sets of gains need 62). Edit (Left / Right / Both) says which set the screen's faders write; the core does not read it.
//   Output (+-12 dB) is the shared frame's. RTA overlay is a display switch; the 1/3-octave levels it shows are rtaDb(band) (sw::ThirdOctaveAnalyzer on the mono sum, 1 s smoothing).
//   Feedback guard (EVO, class B): sw::FeedbackGuard detects without cutting; flaggedBands() is a 31-bit mask of the bands with a howl in them (the screen marks the fader; nothing is cut automatically).
//   Flat: flat() sets all 62 gains to 0 and hands the values to the host through takeParamWrite (not a parameter: "Auto 不可").
#pragma once
#include "sw/bandlevels.hpp"
#include "sw/feedback.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <utility>
#include <vector>

namespace sw::lv12 {

constexpr int kBands = 31;
// parameter order: 31 left gains, then the switches, then the 31 right gains
enum ParamId { Band0 = 0, Edit = kBands, LinkLR, Hpf, Lpf, Output, RtaOverlay, FeedbackGuardOn, BandR0, kNumParams = BandR0 + kBands };
enum EditId { Left = 0, Right = 1, Both = 2 };

const std::vector<ParamSpec>& specs();
double bandCenterHz(int band);

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { if (prepared_) updateAll(0); }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    void flat();
    int takeParamWrite(int& id, double& plain);
    double rtaDb(int band) const { return rta_.levelDb(band); }
    unsigned flaggedBands() const;

private:
    void updateAll(int ramp);
    void updateBand(int c, int b, int ramp);
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    struct Chan { std::array<Svf, kBands> band; std::array<bool, kBands> on{}; Svf hp, lp; };
    std::array<Chan, 2> c_{};
    bool hpOn_ = false, lpOn_ = false;
    FeedbackGuard guard_;
    ThirdOctaveAnalyzer rta_;
    std::vector<std::pair<int, double>> writes_;
    std::vector<float> mono_, fb_;   // scratch (grown only if the host exceeds the announced block size)
};

}  // namespace sw::lv12
