// SW AUDIO core — one EQ band as TPT SVF sections (EQ02 Zero latency / Natural, dynamic parts of Linear)
// Bell / shelves / notch: one section. Cuts: Butterworth cascade, 6..96 dB/oct (12 dB/oct follows Q).
// correctQ (against bilinear cramping near Nyquist): bells get the Q whose digital curve best matches the analog
// prototype below the centre (golden-section fit, cached); notches use the RBJ bandwidth pre-warp.
#pragma once
#include "sw/fir_design.hpp"
#include "sw/svf.hpp"
#include <array>
#include <cmath>

namespace sw {

inline double crampCorrectedQ(double q, double f, double fs) {
    const double w0 = std::min(2.0 * 3.14159265358979323846 * f / fs, 0.98 * 3.14159265358979323846);
    const double bw = 2.0 / std::log(2.0) * std::asinh(1.0 / (2.0 * q));  // analog Q -> bandwidth in octaves
    return 1.0 / (2.0 * std::sinh(std::log(2.0) / 2.0 * bw * w0 / std::sin(w0)));
}

// Q for a TPT bell so that its magnitude matches the analog bell from f0/16 up to f0 (max dB error minimised)
inline double fittedBellQ(double q, double f0, double gainDb, double fs) {
    if (f0 < fs / 16.0 || std::abs(gainDb) < 0.01) return q;  // cramping negligible / flat band
    BandShape a{BandShape::Bell, f0, gainDb, q, 12};
    double fa[32], da[32], wd[32];
    for (int i = 0; i < 32; ++i) {
        fa[i] = f0 / 16.0 * std::pow(16.0, i / 31.0);
        da[i] = 20.0 * std::log10(std::abs(a.responseNorm(fa[i] / f0, q)));
        wd[i] = std::tan(3.14159265358979323846 * fa[i] / fs) / std::tan(3.14159265358979323846 * f0 / fs);
    }
    auto err = [&](double lq) {
        const double qq = std::exp(lq); double e = 0;
        for (int i = 0; i < 32; ++i) e = std::max(e, std::abs(20.0 * std::log10(std::abs(a.responseNorm(wd[i], qq))) - da[i]));
        return e;
    };
    double lo = std::log(q * 0.15), hi = std::log(q * 1.2);
    const double r = 0.6180339887498949;
    double x1 = hi - r * (hi - lo), x2 = lo + r * (hi - lo), e1 = err(x1), e2 = err(x2);
    for (int it = 0; it < 40; ++it) {
        if (e1 < e2) { hi = x2; x2 = x1; e2 = e1; x1 = hi - r * (hi - lo); e1 = err(x1); }
        else { lo = x1; x1 = x2; e1 = e2; x2 = lo + r * (hi - lo); e2 = err(x2); }
    }
    return std::exp(0.5 * (lo + hi));
}

class BandFilter {
public:
    void setup(const BandShape& b, double fs, int ramp, bool correctQ) {
        const double f = std::min(b.freq, 0.49 * fs);
        switch (b.type) {
            case BandShape::Bell: case BandShape::Notch: case BandShape::LowShelf: case BandShape::HighShelf: {
                const Svf::Mode m = b.type == BandShape::Bell ? Svf::Mode::Bell : b.type == BandShape::Notch ? Svf::Mode::Notch
                                  : b.type == BandShape::LowShelf ? Svf::Mode::LowShelf : Svf::Mode::HighShelf;
                const bool bw = b.type == BandShape::Bell || b.type == BandShape::Notch;
                use(1, false);
                double q = b.q;
                if (correctQ && b.type == BandShape::Bell) {  // cached: only refit when the band itself changes
                    if (f != key_[0] || b.q != key_[1] || b.gainDb != key_[2] || fs != key_[3]) { key_[0] = f; key_[1] = b.q; key_[2] = b.gainDb; key_[3] = fs; fitQ_ = fittedBellQ(b.q, f, b.gainDb, fs); }
                    q = fitQ_;
                } else if (correctQ && bw) {
                    q = crampCorrectedQ(b.q, f, fs);
                }
                s_[0].setupRamp(m, f, fs, q, b.gainDb, ramp);
                break;
            }
            case BandShape::LowCut: case BandShape::HighCut: {
                const int order = std::max(1, static_cast<int>(std::lround(b.slope / 6.0)));
                const bool low = b.type == BandShape::LowCut;
                use(order / 2, order % 2 != 0);
                for (int k = 1; k <= order / 2; ++k) {
                    const double qk = order == 2 ? b.q : 1.0 / (2.0 * std::cos((2.0 * k - 1.0) * 3.14159265358979323846 / (2.0 * order)));
                    s_[static_cast<size_t>(k - 1)].setupRamp(low ? Svf::Mode::HighPass : Svf::Mode::LowPass, f, fs, qk, 0, ramp);
                }
                if (odd_) op_.setupRamp(low ? OnePole::Mode::HighPass : OnePole::Mode::LowPass, f, fs, ramp);
                break;
            }
        }
    }
    double process(double x) {
        for (int k = 0; k < n_; ++k) x = s_[static_cast<size_t>(k)].process(x);
        return odd_ ? op_.process(x) : x;
    }
    void reset() { for (auto& s : s_) s.reset(); op_.reset(); }

private:
    void use(int n, bool odd) {
        for (int k = n_; k < n; ++k) s_[static_cast<size_t>(k)].reset();  // sections coming into use start clean
        if (odd && !odd_) op_.reset();
        n_ = n; odd_ = odd;
    }
    std::array<Svf, 8> s_{};
    OnePole op_;
    double key_[4] = {-1, -1, -1, -1}, fitQ_ = 1.0;
    int n_ = 0;
    bool odd_ = false;
};

}  // namespace sw
