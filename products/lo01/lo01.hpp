// SW LO01 Low Harm — makes the lows audible on small speakers through harmonics (spec: 仕様書 v1.0「LO01 Low Harm」)
// Input -> LR4 split at Frequency. Low band -> level-normalised Chebyshev generator (orders 2..N by Width, weights 1 / 0.7 / 0.5 / 0.35,
// total harmonic RMS = Harmonics x the low band's RMS) -> high-pass at 0.5 x Frequency -> added to  Original x low + high band.
// Preview (monitoring only, not automatable) filters the output like a phone speaker or a club system.
#pragma once
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::lo01 {

enum ParamId { Frequency, Harmonics, Original, Width, Preview, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }

private:
    struct Lr4 {
        Svf a, b;
        void setup(Svf::Mode m, double fc, double fs) { a.setup(m, fc, fs, 0.70710678, 0); b.setup(m, fc, fs, 0.70710678, 0); }
        void ramp(Svf::Mode m, double fc, double fs, int n) { a.setupRamp(m, fc, fs, 0.70710678, 0, n); b.setupRamp(m, fc, fs, 0.70710678, 0, n); }
        double process(double x) { return b.process(a.process(x)); }
    };
    struct Ch { Lr4 lp, hp, phoneHp; Svf dc, dc2, bell, clubHp; double env = 1e-5; };
    void updateFilters(bool ramp);
    void updateWeights();
    double fs_ = 48000.0, relDecay_ = 0.0;
    std::array<double, kNumParams> target_{};
    std::array<double, 4> w_{};
    std::array<Ch, 2> c_{};
    LinearSmoother harm_, orig_, pv_;
    bool prepared_ = false;
};

}  // namespace sw::lo01
