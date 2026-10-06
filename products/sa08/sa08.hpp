// SW SA08 Bitcrush — sample-rate and bit-depth reduction (spec: 仕様書 v1.0「SA08 Bitcrush」)
// Input -> [Pre filter: LR4 low-pass at 0.45·Rate] -> sample & hold (period fs/Rate, +-Jitter/2 random) -> quantiser (Bits, optional TPDF dither)
// -> [Post filter: LR4 low-pass at 0.45·Rate]. Mix is the shared frame's. Tempo lock moves the Rate to the nearest integer multiple of the
// beat frequency (bpm/60) when the host gives a tempo. The random parts are seeded at prepare (a render repeats exactly).
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace sw::sa08 {

enum ParamId { Bits, Rate, Jitter, Mix, PreFilter, PostFilter, Dither, TempoLock, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void setTempo(double bpm) { if (bpm != bpm_) { bpm_ = bpm; updateRate(); } }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double effectiveRate() const { return rate_; }   // Hz actually used (Tempo lock applied)

private:
    struct Lr4 {
        Svf a, b;
        void setup(double fc, double fs) { a.setup(Svf::Mode::LowPass, fc, fs, 0.70710678, 0); b.setup(Svf::Mode::LowPass, fc, fs, 0.70710678, 0); }
        double process(double x) { return b.process(a.process(x)); }
    };
    void updateRate();
    double rnd(int ch);   // [-1, 1)
    double fs_ = 48000.0, rate_ = 11000.0, bpm_ = 0.0, counter_ = 0.0, lsb_ = 1.0 / 128.0;
    std::array<double, kNumParams> target_{};
    struct Ch { Lr4 pre, post; double held = 0; uint32_t rng = 1; };
    std::array<Ch, 2> c_{};
    uint32_t jrng_ = 7;
};

}  // namespace sw::sa08
