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
#include "sw/unit.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::dy01 {

enum ParamId { Drive, Ratio, Speed, Bite, Color, Output, Mix, SchPf, Oversample, Unit, kNumParams };

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
        std::array<OsSwitch, 2> os{};   // the common setting (default 2x); Crush runs at 4x (the spec recommends it) unless the setting is 1x
        std::array<double, 2> dc{};
        void reset() { for (auto& o : os) o.reset(); dc = {}; }
    };
    double shape(double u, double g, double b, double& dc, double dcA) const;
    double colorProcess(int ch, double x, double env, double depthDb);
    void updateCurve();
    double fs_ = 48000.0, gr_ = 0, gin_ = 1.0, relax_ = 1.0, fastEnv_ = 0, slowEnv_ = 0;
    std::array<double, 3> dcA_{};   // the DC blocker of the colour stage at 1x / 2x / 4x
    std::array<double, kNumParams> target_{};
    GainComputer comp_;
    Ballistics ball_;
    LinearSmoother drive_;
    std::array<Svf, 2> hpf_{};
    bool hpfOn_ = false, onset_ = false, max_ = false;
    int hold_ = 0;
    double envRel_ = 0, envSlow_ = 0, relaxCoef_ = 0;
    ColorStage color_;
    std::array<double, 2> unitSat_{1.0, 1.0};   // Unit A / B / C: where the colour stage saturates, per channel (a factor on its gain)
};

}  // namespace sw::dy01
