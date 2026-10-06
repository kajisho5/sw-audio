// SW RV02 Plate — plate reverb (spec: 仕様書 v1.0「RV02 Plate」). Wet signal only (Mix is the shared frame's); no reported delay.
//   in (stereo, or the mid with Mono in) -> Pre-delay (ms, or a note length from the host tempo) -> a chain of 48 first-order all-passes with a = -0.7
//   (dispersion: low frequencies are delayed 5.7 samples per stage, high ones 0.18, so the highs reach the ear first, as on a metal plate) -> 16-line FDN (sw::Fdn)
//   -> Width -> Low cut -> Duck (EVO, -6 dB fixed).
#pragma once
#include "sw/fdn.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::rv02 {

enum ParamId { Decay, PreDelay, Damping, LowCut, Width, Mix, MonoIn, Sync, DuckOn, kNumParams };

const std::vector<ParamSpec>& specs();

// group delay (samples) of the dispersion chain at f Hz
double dispersionDelay(double f, double fs);

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void setTempo(double bpm) { bpm_ = bpm; }
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double preDelayMs() const;   // the pre-delay in use (the note length when Sync is on and the tempo is known)

private:
    static constexpr int kStages = 48;
    static constexpr double kA = -0.7;
    struct Chain { std::array<double, kStages> x1{}, y1{}; double process(double x) { for (int k = 0; k < kStages; ++k) { const double y = kA * x + x1[static_cast<size_t>(k)] - kA * y1[static_cast<size_t>(k)]; x1[static_cast<size_t>(k)] = x; y1[static_cast<size_t>(k)] = y; x = y; } return x; } };
    void updateLines();
    void updateFilters();
    double fs_ = 48000.0, bpm_ = 0.0, preLen_ = 0.0, env_ = 0.0, duckGain_ = 1.0, lateTrim_ = 1.0;
    size_t prePos_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    Fdn fdn_;
    std::array<std::vector<float>, 2> pre_;
    std::array<Chain, 2> chain_{};
    std::array<Svf, 2> hp_{};
    LinearSmoother width_;
};

}  // namespace sw::rv02
