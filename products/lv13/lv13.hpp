// SW LV13 Live Peq — a 6-band live parametric EQ (spec: 仕様書 v1.0「LV13 Live Peq」). Reported delay 0. Each band: Type Bell / Shelf, Freq, Gain +-15 dB, Q 0.3..10; HPF (Off, 20..400 Hz) and LPF (5..20 kHz, Off at the top), both 2nd order.
//   Shelf (design, the same rule as EQ07): under 1 kHz a low shelf, from 1 kHz up a high shelf (Q = the slope); the default band frequencies spread 100 Hz .. 10 kHz in equal ratios (100, 250, 630, 1.6 k, 4 k, 10 k).
//   EVO (class B): the real-time analyser (sw::ThirdOctaveAnalyzer, 1 s smoothing) is always running; suggestions() lists up to 3 places to cut: a 1/3-octave band that stands 6 dB or more over the mean of the bands 2 to either side
//   (and over the 1 s level): the frequency, a cut of half the excess up to -6 dB, and Q 4. It is advice for the screen ("ここを削る"); nothing is written to the bands.
#pragma once
#include "sw/bandlevels.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::lv13 {

constexpr int kBands = 6;
enum Field { Type, Freq, Gain, Q, kPerBand };
// order: band 1 (Type, Freq, Gain, Q), band 2 ..., then HPF, LPF
enum ParamId { Band1 = 0, Hpf = kBands * kPerBand, Lpf, kNumParams };
constexpr int id(int band, int field) { return band * kPerBand + field; }   // band 0..5
enum TypeId { Bell = 0, Shelf = 1 };

struct Suggestion { double freqHz, gainDb, q; };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { if (prepared_) updateAll(0); }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    std::vector<Suggestion> suggestions() const;
    double rtaDb(int band) const { return rta_.levelDb(band); }

private:
    void updateBand(int b, int ramp);
    void updateAll(int ramp);
    double fs_ = 48000.0;
    bool prepared_ = false, hpOn_ = false, lpOn_ = false;
    std::array<double, kNumParams> target_{};
    struct Chan { std::array<Svf, kBands> band; Svf hp, lp; };
    std::array<Chan, 2> c_{};
    std::array<bool, kBands> on_{};
    ThirdOctaveAnalyzer rta_;
    std::vector<float> mono_;
};

}  // namespace sw::lv13
