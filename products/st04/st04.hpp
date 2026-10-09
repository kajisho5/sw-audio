// SW ST04 Center — center / width / Haas (spec: 仕様書 v1.0「ST04 Center」). The whole signal is processed (no Mix); reported delay 0.
//   M = (L + R) / 2, S = (L - R) / 2. Center (Wide .. Focus, 0 .. 100 %): the gain of S: left half +6 .. 0 dB (6 (1 - x / 50) dB), right half 0 .. -inf (20 log10 (1 - (x - 50) / 50)), 100 % = mono.
//   Low center (0 .. 10; 0 = Off, else 20 x 15^(v / 10) Hz = 26 .. 300 Hz): S through a 4th-order high-pass (everything below goes mono). L = M + S, R = M - S.
//   Haas (0 .. 40 ms, k = 2) delays the chosen Side (L or R). Link (On): the delayed side is raised by 0.35 dB per ms (at most 6 dB) so that the earlier side does not pull the image. Balance (L .. R, +-100 %):
//   linear, the louder side stays, the other goes down to 0.
//   Mono safe (st04.evo.on, default Off; class A): the delayed copy summed in mono makes a comb whose first notch is 20 log10 ((1 - r) / (1 + r)) dB deep (r = delayed level / other level at the end of the chain).
//   If that is deeper than -10 dB (r > 0.52) the delayed side is lowered to r = 0.52 and low-passed at 20 kHz x (0.52 / r)^2 (not below 3 kHz).
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::st04 {

enum ParamId { Center, Haas, Side, LowCenter, Balance, Link, MonoSafe, Unit, kNumParams };

const std::vector<ParamSpec>& specs();
double sideGain(double center);            // Center 0 .. 100 -> linear gain of S
double lowCenterHz(double v);              // 0 .. 10 -> 0 (Off) or 26 .. 300 Hz
constexpr double kMaxHaasMs = 40.0, kLinkDbPerMs = 0.35, kLinkMaxDb = 6.0, kNotchLimitDb = -10.0;

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double monoCombDepthDb() const;     // predicted first-notch depth of the mono sum for the settings in use (0 without Haas)
    double delayedSideGainDb() const;   // gain of the delayed side after Link and Mono safe, against the other side

private:
    void update();
    double read(int c, double delay) const;
    double fs_ = 48000.0, haasT_ = 0.0, haasD_ = 0.0, gL_ = 1.0, gR_ = 1.0, gLt_ = 1.0, gRt_ = 1.0, lpHz_ = -1.0;
    size_t pos_ = 0, mask_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<std::vector<float>, 2> buf_;
    Svf hp1_, hp2_, lp_;
    double hpHz_ = -1.0;
};

}  // namespace sw::st04
