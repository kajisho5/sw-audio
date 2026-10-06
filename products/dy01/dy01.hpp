// SW DY01 FET — character compressor, input pushed into a fixed threshold (spec: 仕様書 v1.0「DY01 FET 進化版」)
// Drive 0..10 = input 0..+36 dB into a fixed -6 dBFS threshold. Speed links attack 800..20 us and release 1100..50 ms.
// Detection is peak-based and linked; the feedback law's static solution (gain = -(1 - 1/R)(level - T)) is evaluated
// directly, because a one-sample-delayed loop is unstable for R = 20 at a 20 us attack (< 1 sample at 48 kHz).
// EVO Bite: onset detector relaxes the gain reduction for 5..15 ms. Color: amplifier-stage distortion at 2x OS (Crush: 4x).
#pragma once
#include "sw/dynamics.hpp"
#include "sw/oversample.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::dy01 {

enum ParamId { Drive, Ratio, Speed, Bite, Color, Output, Mix, SchPf, kNumParams };

const std::vector<ParamSpec>& specs();
double attackMs(double speed);   // 800 .. 20 us (log-linear in Speed)
double releaseMs(double speed);  // 1100 .. 50 ms

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { drive_.skip(1 << 30); gin_ = std::pow(10.0, drive_.current() / 20.0); }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double gainReductionDb() const { return gr_; }

private:
    struct ColorStage {
        std::array<Oversampler2x, 2> a{}, b{};  // 2x; Crush cascades b for 4x
        std::array<double, 2> dc2{}, dc4{};
        void reset() { a = {}; b = {}; dc2 = {}; dc4 = {}; }
    };
    double shape(double u, double g, double b, double& dc, double dcA) const;
    double colorProcess(int ch, double x, double env, double depthDb);
    void updateCurve();
    double fs_ = 48000.0, gr_ = 0, gin_ = 1.0, relax_ = 1.0, fastEnv_ = 0, slowEnv_ = 0, dcA2_ = 0, dcA4_ = 0;
    std::array<double, kNumParams> target_{};
    GainComputer comp_;
    Ballistics ball_;
    LinearSmoother drive_;
    std::array<Svf, 2> hpf_{};
    bool hpfOn_ = false, onset_ = false, max_ = false;
    int hold_ = 0;
    double envRel_ = 0, envSlow_ = 0, relaxCoef_ = 0;
    ColorStage color_;
};

}  // namespace sw::dy01
