// SW LV04 Safety limiter — LIVE line (spec: 仕様書 v1.0「LV04 Safety limiter」)
// Subsonic HPF -> long-term RMS limit (speaker protection) -> peak stage:
//   Zero      : instantaneous linked peak limiter, 0 latency, sample peaks can never exceed the ceiling
//   True peak : look-ahead (1.5 ms) true-peak limiter, latency shown in the LIVE toolbar
// EVO: every limit event is logged with its start time, length and depth (ring of 256 for the UI / CSV).
#pragma once
#include "sw/dynamics.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::lv04 {

enum ParamId { Mode, Ceiling, Release, RmsLimit, Subsonic, kNumParams };

const std::vector<ParamSpec>& specs();

struct LimitEvent { long long startSample = 0; long long lengthSamples = 0; double maxReductionDb = 0; };

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const;  // desired (Mode); applied at prepare
    int eventCount() const { return static_cast<int>(std::min<long long>(events_, kLog)); }
    long long totalEvents() const { return events_; }
    LimitEvent event(int i) const;  // 0 = oldest kept

private:
    static constexpr int kLog = 256;
    void logLimiting(double gain);
    double fs_ = 48000.0;
    std::array<double, kNumParams> target_{};
    bool tp_ = false, subOn_ = true, inEvent_ = false;
    std::array<Svf, 2> sub_{};
    double ms_ = 0, msCoef_ = 0, ceil_ = 1, relCoef_ = 0, r_ = 1;
    Ballistics rms_;
    PeakLimiter lim_;
    long long t_ = 0, events_ = 0;
    std::array<LimitEvent, kLog> log_{};
};

}  // namespace sw::lv04
