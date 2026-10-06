// SW AUDIO core — four-band 4th-order Linkwitz-Riley split (DY10 / MS03 family): three crossovers, bands add back flat in magnitude.
//   b1 = AP3(AP2(LP1 x)), b2 = AP3(LP2(HP1 x)), b3 = LP3(HP2(HP1 x)), b4 = HP3(HP2(HP1 x)); AP = LP + HP of the same LR4 crossover.
// Crossovers keep an octave apart (from the lowest up; the mover is pushed back) and the top is capped at min(20 kHz, 0.45 fs).
#pragma once
#include "sw/svf.hpp"
#include <algorithm>
#include <array>

namespace sw {

class Lr4Split4 {
public:
    // effective crossover frequencies after the octave rule
    static std::array<double, 3> effective(double f1, double f2, double f3, double fs) {
        const double top = std::min(20000.0, fs * 0.45);
        f2 = std::max(f2, 2.0 * f1); f3 = std::max(f3, 2.0 * f2);
        f3 = std::min(f3, top); f2 = std::min(f2, f3 / 2.0); f1 = std::min(f1, f2 / 2.0);
        return {f1, f2, f3};
    }
    void setup(double f1, double f2, double f3, double fs) {
        for (auto& x : ch_) {
            x.lp1.setup(Svf::Mode::LowPass, f1, fs); x.hp1.setup(Svf::Mode::HighPass, f1, fs);
            x.a2lo.setup(Svf::Mode::LowPass, f2, fs); x.a2hi.setup(Svf::Mode::HighPass, f2, fs);
            x.a3lo.setup(Svf::Mode::LowPass, f3, fs); x.a3hi.setup(Svf::Mode::HighPass, f3, fs);
            x.lp2.setup(Svf::Mode::LowPass, f2, fs); x.hp2.setup(Svf::Mode::HighPass, f2, fs);
            x.b3lo.setup(Svf::Mode::LowPass, f3, fs); x.b3hi.setup(Svf::Mode::HighPass, f3, fs);
            x.lp3.setup(Svf::Mode::LowPass, f3, fs); x.hp3.setup(Svf::Mode::HighPass, f3, fs);
        }
    }
    void process(int channel, double in, double out[4]) {
        Xover& x = ch_[static_cast<size_t>(channel)];
        const double rest = x.hp1.process(in);
        const double low = x.lp1.process(in);
        const double l2 = x.a2lo.process(low) + x.a2hi.process(low);
        out[0] = x.a3lo.process(l2) + x.a3hi.process(l2);
        const double m = x.lp2.process(rest);
        out[1] = x.b3lo.process(m) + x.b3hi.process(m);
        const double rest2 = x.hp2.process(rest);
        out[2] = x.lp3.process(rest2);
        out[3] = x.hp3.process(rest2);
    }

private:
    struct Lr4 {
        Svf a, b;
        void setup(Svf::Mode m, double fc, double fs) { a.setup(m, fc, fs, 0.70710678, 0); b.setup(m, fc, fs, 0.70710678, 0); }
        double process(double x) { return b.process(a.process(x)); }
    };
    struct Xover { Lr4 lp1, hp1, a2lo, a2hi, a3lo, a3hi, lp2, hp2, b3lo, b3hi, lp3, hp3; };
    std::array<Xover, 2> ch_{};
};

}  // namespace sw
