// SW EQ06 Stepped — 3-band stepped EQ with proportional Q (spec: 仕様書 v1.0「EQ06 Stepped」)
// Gains move in 2 dB steps; EVO Glide moves gain, Q and frequency over 30 ms so steps never click.
#pragma once
#include "sw/oversample.hpp"
#include "sw/param.hpp"
#include "sw/saturate.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::eq06 {

enum ParamId { LowHz, LowGain, MidHz, MidGain, HighHz, HighGain, Shape, Drive, Output, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    double getParam(int id) const { return target_[static_cast<size_t>(id)]; }
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }

private:
    void update(int ramp);
    double fs_ = 48000.0;
    std::array<double, kNumParams> target_{};
    LinearSmoother lowG_, midG_, highG_, lowF_, midF_, highF_, shelf_, drive_;
    struct Ch { Svf lowPk, lowSh, mid, highPk, highSh; Oversampler2x os; };
    std::array<Ch, 2> ch_{};
    Saturator sat_;
};

}  // namespace sw::eq06
