// SW DY11 Multiband 6 — six moving filters in series, per-band Compress / Expand / Dynamic EQ (spec: 仕様書 v1.0「DY11 Multiband 6」)
// No band split: each band is a dynamic bell (the end bands are shelves) whose gain follows its own detector, so the modes mix
// freely and the latency is 0. Compress / Expand: a wide bell reaching to the geometric midpoint of the neighbouring bands
// (bands 1 and 6: shelves at the midpoint with their neighbour). Dynamic EQ: a bell of Width octaves at Freq.
// Detector: band-pass (the band's Q) or the shelf side, RMS 10 ms, linked over both channels.
#pragma once
#include "sw/bandfilter.hpp"
#include "sw/dynamics.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::dy11 {

constexpr int kBands = 6;
enum BandParam { BMode, BFreq, BThresh, BRatio, BAttack, BRelease, BRange, BGain, BWidth, kPerBand };
constexpr int band(int n, int k) { return n * kPerBand + k; }   // n = 0..5
enum ParamId { Output = kBands * kPerBand, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { if (prepared_) configure(0); }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double gainReductionDb(int b) const { return band_[static_cast<size_t>(b)].gr; }

private:
    static constexpr int kControl = 16;
    double t(int b, int f) const { return target_[static_cast<size_t>(band(b, f))]; }
    int mode(int b) const { return static_cast<int>(t(b, BMode) + 0.5); }
    struct Region { BandShape shape; Svf::Mode detMode; double detFreq, detQ; };
    Region region(int b, double gainDb) const;
    void configure(int ramp);
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    struct Band {
        std::array<BandFilter, 2> f{};
        std::array<Svf, 2> det{};
        std::array<LevelDetector, 2> lvl{};
        double gr = 0, applied = 1e9;
    };
    std::array<Band, kBands> band_{};
};

}  // namespace sw::dy11
