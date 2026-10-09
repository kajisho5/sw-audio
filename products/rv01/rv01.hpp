// SW RV01 Hall — algorithmic reverb (spec: 仕様書 v1.0「RV01 Hall」). Wet signal only (Mix is the shared frame's); no reported delay (the reverb's own delay is part of the sound).
//   mid -> Pre-delay -> [early reflections: tapped delay, per algorithm, scaled by Size] + [4 Schroeder all-passes (Diffusion) -> 16-line FDN (sw::Fdn)]
//   -> ER / late mix -> Width (M/S) -> Mono low end (sides high-passed at 120 Hz) -> Low cut / High cut -> Duck (wet level follows the dry level) -> out.
#pragma once
#include "sw/fdn.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::rv01 {

enum ParamId { Algorithm, PreDelay, Size, Decay, Diffusion, Damping, LowCut, HighCut, ErLate, Width, Mix, Freeze, MonoLow, Duck, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    double tailSeconds() const;   // how long it goes on after the input stops (sw/tail.hpp)
    int latencySamples() const { return 0; }

private:
    struct Tap { double ms, gain, pan; };
    struct Ap { std::vector<float> buf; size_t pos = 0; int len = 1; double process(double x, double g) { const double d = buf[pos]; const double y = -g * x + d; buf[pos] = static_cast<float>(x + g * y); if (++pos >= buf.size()) pos = 0; return y; } };
    void updateLines();
    void updateFilters();
    double fs_ = 48000.0, preLen_ = 0.0, env_ = 0.0, duckGain_ = 1.0, lateTrim_ = 1.0;
    size_t prePos_ = 0, erPos_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    Fdn fdn_;
    std::vector<float> pre_, erBuf_;
    std::array<Ap, 4> ap_{};
    std::array<std::array<double, 12>, 2> tapGain_{};   // smoothed per-tap gain for L and R
    std::array<Svf, 2> hp_{}, lp_{};
    Svf sideHp_[2];
    LinearSmoother width_, lateG_, erG_;
};

}  // namespace sw::rv01
