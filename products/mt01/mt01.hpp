// SW MT01 Loudness — a loudness meter (spec: 仕様書 v1.0「MT01 Loudness」). The signal passes unchanged (bit for bit); reported delay 0; no Delta / Auto gain (traits kAutoGain = false, kDelta = false).
//   Momentary (400 ms) and Short-term (3 s) from the BS.1770 K-weighted 100 ms blocks (sw::LoudnessMeter), Integrated with the absolute (-70 LUFS) and relative (-10 LU) gates (sw::IntegratedLoudness), the loudness range
//   (EBU Tech 3342: the short-term values every 100 ms, absolute gate -70 LUFS, relative gate -20 LU under their power mean, LRA = the 95th - the 10th percentile, in 0.1 LU bins), the true peak (4x Kaiser
//   interpolation, sw::TruePeakDetector, dBTP), a history of the short-term value once a second for 10 minutes. Preset (the target): ARIB TR-B32 -24 / EBU R128 -23 / Streaming -14 / Custom (Target, -40 .. -5 LUFS).
//   difference() = Integrated - target; inBand() = |difference| <= Tolerance (0.5 .. 3 LU, the width of the displayed band; whether +-1 LU is the standard's own tolerance is open in the spec).
//   Pause (parameter, not automatable) stops the measurement; reset() clears it (the UI button). The values are published for the UI after every block.
#pragma once
#include "sw/dynamics.hpp"
#include "sw/loudness.hpp"
#include "sw/param.hpp"
#include <array>
#include <vector>

namespace sw::mt01 {

enum ParamId { Preset, Target, Tolerance, Pause, kNumParams };
enum PresetId { Arib = 0, Ebu = 1, Streaming = 2, Custom = 3 };

const std::vector<ParamSpec>& specs();
double presetTarget(int preset, double custom);
constexpr int kHistory = 600;

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    void reset();
    double momentary() const { return momentary_; }
    double shortTerm() const { return shortTerm_; }
    double integrated() const { return integrated_; }
    double range() const;                      // LRA in LU
    double truePeakDb() const;                 // dBTP
    double target() const { return presetTarget(static_cast<int>(target_[Preset] + 0.5), target_[Target]); }
    double difference() const { return integrated_ > -150.0 ? integrated_ - target() : 0.0; }
    bool inBand() const { return integrated_ > -150.0 && std::abs(difference()) <= target_[Tolerance]; }
    const std::vector<float>& history() const { return history_; }   // the short-term value once a second, oldest first, at most kHistory

private:
    static constexpr size_t kLraBins = 701;
    double fs_ = 48000.0, momentary_ = -200.0, shortTerm_ = -200.0, integrated_ = -200.0, tp_ = 0.0;
    int sinceHist_ = 0, sinceLra_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    LoudnessMeter meter_;
    IntegratedLoudness integ_;
    std::array<TruePeakDetector, 2> tpd_;
    std::vector<double> lraCount_, lraSum_;
    std::vector<float> history_;
};

}  // namespace sw::mt01
