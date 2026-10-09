// SW DY06 Vari-Mu — variable-mu tube compressor (spec: 仕様書 v1.0「DY06 Vari-Mu」)
// Input -10..+20 dB (knob 3.3 = 0 dB) -> tube stage (even-order asymmetric saturation at 2x OS: sw::DriveStage, fixed Drive 3)
// -> gain element whose ratio rises with depth (about 1.5:1 -> 6:1, Mu sets how fast). Time 1..6 = attack / release table.
// EVO Density adapt: release x 0.5 (sparse) .. x 2 (dense) from the 2 s crest factor and the onset rate.
#pragma once
#include "sw/drive.hpp"
#include "sw/dynamics.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/unit.hpp"
#include <array>
#include <vector>

namespace sw::dy06 {

enum ParamId { Input, Threshold, Time, Mu, Mix, Stereo, Density, Oversample, Unit, kNumParams };

const std::vector<ParamSpec>& specs();
double inputDb(double knob);   // -10 + 3 * knob
double attackMs(int time);     // 2 2 4 8 4 2

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { in_.skip(1 << 30); }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double gainReductionDb() const { return gr_; }
    double thresholdKnob() const { return target_[Threshold]; }
    double releaseScale() const { return relScale_; }

private:
    struct Chan {
        LevelDetector det;
        double fast = 0, slow = 0;
        void reset() { det.reset(); fast = slow = 0; }
    };
    void buildCurve();
    void updateTiming();
    double staticGr(double over) const;
    double fs_ = 48000.0, gr_ = 0, relScale_ = 1.0;
    std::array<double, kNumParams> target_{};
    std::vector<double> curve_;                 // gain reduction (dB, <= 0) over -6 .. +60 dB at 0.1 dB steps
    std::array<Chan, 2> ch_{};
    LinearSmoother in_;
    double inGain_ = 1.0;
    double attackC_ = 0, fastRelC_ = 0, slowRelC_ = 0, slowAttC_ = 0, fixedRelS_ = 1.0;
    bool autoRel_ = false;
    // density adapt
    double pk_ = 0, ms_ = 0, pkC_ = 0, msC_ = 0, envFast_ = 0, envSlow_ = 0, envFastC_ = 0, envSlowC_ = 0, onsetRate_ = 0, onsetC_ = 0, scaleC_ = 0;
    bool onset_ = false;
    std::array<DriveStage, 2> tubeCh_{};
};

}  // namespace sw::dy06
