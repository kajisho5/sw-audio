// SW SA06 Saturator — five distortions in three bands (spec: 仕様書 v1.0「SA06 Saturator」)
// LR4 split at 200 Hz and 3 kHz (the bands add back flat). Per band: Drive (0..+24 dB) into Tape / Tube / Diode / Fold / Fuzz (4x OS, Fold and
// Fuzz 8x), Shape (soft / medium / hard), Bias, Dynamics (EVO: the drive follows the band's level, + = louder notes distort more,
// - = softer notes distort more), Mix. Tone tilts the sum around 1 kHz. Every shape has a small-signal slope of 1 at its operating point.
#pragma once
#include "sw/lr4split.hpp"
#include "sw/oversample.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::sa06 {

enum BandParam { BType, BDrive, BShape, BBias, BDyn, BMix };
constexpr int band(int n, int k) { return n * 6 + k; }   // n = 0..2
enum ParamId { Tone = 18, Output, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { for (auto& d : drive_) d.skip(1 << 30); }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }

private:
    template <int N> struct OsChain {
        std::array<Oversampler2x, N> os{};
        template <class F> double run(int level, double x, F& f) {
            if (level == N) return f(x);
            double up[2]; os[static_cast<size_t>(level)].up(x, up);
            for (double& u : up) u = run(level + 1, u, f);
            return os[static_cast<size_t>(level)].down(up);
        }
        template <class F> double process(double x, F& f) { return run(0, x, f); }
    };
    struct BandCh { OsChain<2> x4; OsChain<3> x8; double dc = 0, env = 0; };
    void updateTone();
    double fs_ = 48000.0, dcA_ = 0, envC_ = 0;
    std::array<double, kNumParams> target_{};
    Lr4Split3 split_;
    std::array<std::array<BandCh, 2>, 3> bc_{};
    std::array<LinearSmoother, 3> drive_;
    struct ToneCh { Svf lo, hi; };
    std::array<ToneCh, 2> tone_{};
};

}  // namespace sw::sa06
