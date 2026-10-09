// SW ST02 Mid Side — mid/side level and tone (spec: 仕様書 v1.0「ST02 Mid Side」). The whole signal is processed (no Mix); reported delay 0.
//   M = (L + R) / 2, S = (L - R) / 2; Mid level / Side level (+-12 dB); Side HPF (20 Hz = Off, up to 500 Hz, 2nd order, S only); Side air (S high shelf at 10 kHz, 0 .. +6 dB);
//   Mid low (M low shelf at 100 Hz, +-6 dB); L = M + S, R = M - S. Encode (On): the input already is M (left) and S (right) and the output stays M (left) / S (right)
//   (the same processing without the encode and decode). EVO (class B, reference comparison) belongs to the screen.
#pragma once
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::st02 {

enum ParamId { MidLevel, SideLevel, SideHpf, SideAir, MidLow, Encode, Unit, kNumParams };

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
    void update();
    double fs_ = 48000.0, midG_ = 1.0, sideG_ = 1.0, midT_ = 1.0, sideT_ = 1.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    Svf hpf_, air_, low_;
    double hpfHz_ = -1, airDb_ = -99, lowDb_ = -99;
};

}  // namespace sw::st02
