// SW SA05 Exciter — harmonics above Tune (spec: 仕様書 v1.0「SA05 Exciter」)
// The band above Tune (LR4 high-pass) is turned into its 2nd (even: (x^2 - mean square) / rms) and 3rd (odd: x^3 / mean square - 1.5 x)
// harmonics at 2x OS; both are normalised by the band's own level, so the amount does not depend on how loud the input is. The result
// is high-passed again at Tune and added to the input (Harmonics = amount; the shared Mix blends wet and dry). Low drive does the same for
// the lows (below 200 Hz), Mono low makes those from the mid. EVO Auto fill (class B): 1/3-octave levels above Tune are compared with a
// gently falling target (anchored on the bands below Tune) and the harmonics get up to +12 dB where the input lacks highs.
#pragma once
#include "sw/bandlevels.hpp"
#include "sw/oversample.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::sa05 {

enum ParamId { Tune, Harmonics, Mix, LowDrive, Mode, MonoLow, AutoFill, Oversample, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double fillDb(int region) const { return fill_[static_cast<size_t>(region)]; }   // Auto fill gain of the three regions (dB)

private:
    struct Lr4 {
        Svf a, b;
        void setup(Svf::Mode m, double fc, double fs) { a.setup(m, fc, fs, 0.70710678, 0); b.setup(m, fc, fs, 0.70710678, 0); }
        double process(double x) { return b.process(a.process(x)); }
    };
    struct Gen {   // normalised even / odd generator at 2x
        OsSwitch os;   // the common setting (default 2x)
        double ms = 0;
        double process(double x, int mode, double msC);
    };
    void updateFilters();
    void applyOversample();
    void analyse();
    double fs_ = 48000.0, msC_ = 0, fillC_ = 0;
    std::array<double, kNumParams> target_{};
    std::array<Lr4, 2> hp_{}, post_{};
    std::array<Gen, 2> genHi_{}, genLo_{};
    std::array<std::array<Svf, 2>, 2> lows_{};                // 200 Hz low-pass x2 per channel
    std::array<Svf, 2> lowHp_{};                              // removes the DC / fundamental left in the low harmonics
    struct Fill { Svf r1, r2, r3; };
    std::array<Fill, 2> fillEq_{};
    std::array<double, 3> fill_{}, fillWant_{};
    ThirdOctaveAnalyzer an_;
    int sinceAnalysis_ = 0;
};

}  // namespace sw::sa05
