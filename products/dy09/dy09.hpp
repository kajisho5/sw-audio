// SW DY09 Transient — level-independent attack / sustain shaper (spec: 仕様書 v1.0「DY09 Transient」)
// Three envelope followers of |x| (fast / slow / slowest; Speed sets all three). Head = fast vs slow (dB ratio, onsets), tail = slow vs
// slowest (dB ratio, decays), so the amount does not depend on the input level. Gain(dB) = Attack x clamp(r1/12) + Sustain x clamp(-r2/12).
// Split bands: LR4 crossovers at 150 Hz and 4 kHz (low band all-passed by the 4 kHz crossover so the three bands add back flat).
// Clip: 2x oversampled soft (tanh) / hard clip at 0 dBFS.
#pragma once
#include "sw/oversample.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::dy09 {

enum ParamId { Attack, Sustain, Speed, Clip, Mix, Mode, B1Attack, B1Sustain, B2Attack, B2Sustain, B3Attack, B3Sustain, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }

private:
    struct Lr4 {
        Svf a, b;
        void setup(Svf::Mode m, double fc, double fs) { a.setup(m, fc, fs, 0.70710678, 0); b.setup(m, fc, fs, 0.70710678, 0); }
        double process(double x) { return b.process(a.process(x)); }
    };
    struct Env {
        double fast = 0, slow = 0, slowest = 0, gain = 0;
        void reset() { fast = slow = slowest = gain = 0; }
    };
    struct Split { Lr4 lp150, hp150, lpA, hpA, lpB, hpB; };
    double shapeDb(Env& e, double level, double att, double sus) const;
    void updateSpeed();
    double fs_ = 48000.0, cF_ = 0, cS_ = 0, cSS_ = 0, cG_ = 0;
    std::array<double, kNumParams> target_{};
    std::array<Env, 4> env_{};
    std::array<Split, 2> split_{};
    std::array<std::array<Oversampler2x, 2>, 1> os_{};
};

}  // namespace sw::dy09
