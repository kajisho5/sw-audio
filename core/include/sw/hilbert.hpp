// SW AUDIO core — IIR Hilbert pair (MD06 Freq Shift): two chains of four second-order all-passes in z^2 (Olli Niemitalo's 12-pole design, 90 degrees +-0.7 over 0.002 .. 0.998 of Nyquist).
//   process(x, i, q): i and q have equal magnitude, q LEADS i by 90 degrees (chain 1 is delayed one sample after its all-passes; measured 90 +-0.5 degrees at 80 Hz .. 18 kHz at 48 kHz). No latency is added: the pair is as close as an IIR gets to
//   an analytic signal, so that i + j q is the input shifted in phase by an unknown (frequency-dependent) amount, which a frequency shifter does not care about.
#pragma once
#include <cstddef>

namespace sw {

class HilbertIir {
public:
    void reset() { for (auto& s : sec_) s = Section{}; i1_ = 0.0; }
    void process(double x, double& i, double& q) {
        double a = x, b = x;
        for (int k = 0; k < 4; ++k) a = sec_[static_cast<std::size_t>(k)].run(a, kCoef1[k] * kCoef1[k]);
        for (int k = 0; k < 4; ++k) b = sec_[static_cast<std::size_t>(4 + k)].run(b, kCoef2[k] * kCoef2[k]);
        i = i1_; i1_ = a; q = b;
    }

private:
    struct Section {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        double run(double x, double a2) { const double y = a2 * (x + y2) - x2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }
    };
    static constexpr double kCoef1[4] = {0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737};
    static constexpr double kCoef2[4] = {0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278};
    Section sec_[8];
    double i1_ = 0.0;
};

}  // namespace sw
