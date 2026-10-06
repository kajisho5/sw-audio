// SW DY05 De-ess — sibilance only (spec: 仕様書 v1.0「DY05 De-ess」)
// Detection: energy above Freq (4th-order high-pass) relative to the full-band energy decides WHAT is sibilance (so it does not
// depend on loudness); the high-pass level against Threshold decides HOW MUCH, up to Range. Split: y = LP(x) + g * HP(x) with a
// Linkwitz-Riley 4th-order crossover (the two bands add back flat in magnitude; the phase is the crossover's all-pass, also at g = 1).
// Wide: y = g * x. Lookahead delays the main path.
// EVO Pitch follow: an autocorrelation pitch tracker (70..1000 Hz) marks voiced spans, where the decision is made less sensitive.
#pragma once
#include "sw/dynamics.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::dy05 {

enum ParamId { Mode, Freq, Threshold, Range, Lookahead, Listen, Pitch, kNumParams };

const std::vector<ParamSpec>& specs();

class PitchTracker {
public:
    void prepare(double fs);
    void push(double x);                  // one input sample (mono)
    bool voiced() const { return voiced_; }
    double f0() const { return f0_; }     // last tracked fundamental (Hz), 0 when none yet
    void reset();
private:
    void analyse();
    double fd_ = 6000.0;
    int decim_ = 8, phase_ = 0, hop_ = 0, pos_ = 0;
    double lp1_ = 0, lp2_ = 0, lpC_ = 0, f0_ = 0; int sinceHop_ = 0;
    bool voiced_ = false;
    std::vector<double> ring_;
};

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const;           // Lookahead; applied at prepare
    double gainReductionDb() const { return gr_; }
    double voiceF0() const { return pitch_.f0(); }  // tracked fundamental, for the Freq suggestion in the UI

private:
    double fs_ = 48000.0, gr_ = 0, hfE_ = 0, fullE_ = 0, energyC_ = 0, attackC_ = 0, releaseC_ = 0;
    std::array<double, kNumParams> target_{};
    std::array<std::array<Svf, 2>, 2> hp_{}, lp_{};
    std::array<std::vector<float>, 2> dx_, dh_, dl_;   // delayed input, high band and low band
    int lat_ = 0, pos_ = 0;
    PitchTracker pitch_;
    double voicedW_ = 1.0;
};

}  // namespace sw::dy05
