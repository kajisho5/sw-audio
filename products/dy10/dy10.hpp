// SW DY10 Multiband 4 — four-band compressor (spec: 仕様書 v1.0「DY10 Multiband 4」)
// 4th-order Linkwitz-Riley split at three crossovers, with the all-pass partner filters so the bands add back flat. Each band:
// feed-forward RMS (10 ms) detector linked over both channels, soft knee 6 dB, Ranged gain reduction, Attack / Release, Gain, Solo, Bypass.
// Crossovers keep an octave apart: from the lowest up, x(n+1) is at least 2 x(n) (the mover is pushed back); the top is capped at 20 kHz.
#pragma once
#include "sw/dynamics.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::dy10 {

enum BandParam { BThresh, BRatio, BAttack, BRelease, BRange, BGain, BSolo, BBypass };
constexpr int band(int n, int k) { return n * 8 + k; }   // n = 0..3
enum ParamId { X1 = 32, X2, X3, Output, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double gainReductionDb(int band) const { return gr_[static_cast<size_t>(band)]; }
    double crossoverHz(int i) const { return eff_[static_cast<size_t>(i)]; }   // effective (after the octave rule)

private:
    struct Lr4 {
        Svf a, b;
        void setup(Svf::Mode m, double fc, double fs) { a.setup(m, fc, fs, 0.70710678, 0); b.setup(m, fc, fs, 0.70710678, 0); }
        double process(double x) { return b.process(a.process(x)); }
    };
    struct Xover {  // one channel
        Lr4 lp1, hp1, a2lo, a2hi, a3lo, a3hi, lp2, hp2, b3lo, b3hi, lp3, hp3;
    };
    void updateCrossovers();
    void updateBand(int n);
    double fs_ = 48000.0;
    std::array<double, kNumParams> target_{};
    std::array<double, 3> eff_{};
    std::array<Xover, 2> xo_{};
    std::array<std::array<LevelDetector, 4>, 2> det_{};
    std::array<GainComputer, 4> comp_{};
    std::array<Ballistics, 4> ball_{};
    std::array<double, 4> gr_{};
};

}  // namespace sw::dy10
