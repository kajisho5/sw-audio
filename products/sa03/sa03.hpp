// SW SA03 Tube — tube harmonics (spec: 仕様書 v1.0「SA03 Tube」)
// Input -> drive into a biased tanh (sw::BiasShaper4x, small-signal gain 1; Tube sets the bias, headroom and how hard it drives) -> Tone
// (a tilt of +-6 dB around 1 kHz). Bias Cold..Hot moves the operating point (Cold = symmetric). EVO moving bias: the input envelope
// (instant attack, 50 ms return) pushes the bias further, so louder notes get more asymmetric. Mix and Output are the shared frame's.
#pragma once
#include "sw/param.hpp"
#include "sw/shaper.hpp"
#include "sw/svf.hpp"
#include "sw/unit.hpp"
#include <array>
#include <vector>

namespace sw::sa03 {

enum ParamId { Drive, Bias, Tone, Tube, Mix, Output, Evo, Oversample, Unit, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double biasShift() const { return shift_; }   // the extra bias the envelope adds right now

private:
    void updateTone();
    double fs_ = 48000.0, env_ = 0, envC_ = 0, shift_ = 0;
    std::array<double, kNumParams> target_{};
    BiasShaper4x shaper_;
    int unit_ = 0;   // Unit A / B / C (sw/unit.hpp): slot 0 the tone pivot, and the shaper
    void applyUnit();
    struct ToneCh { Svf lo, hi; };
    std::array<ToneCh, 2> tone_{};
};

}  // namespace sw::sa03
