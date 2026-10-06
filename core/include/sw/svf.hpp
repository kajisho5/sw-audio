// SW AUDIO core — TPT state variable filter (spec: EQのフィルタは TPT SVF、係数をサンプルごとに補間しても発振しない)
// Topology-preserving (trapezoidal) SVF after A. Simper, "Linear Trapezoidal Integrated SVF".
// The response equals the analog prototype evaluated at the bilinear-warped frequency.
#pragma once
#include <cmath>

namespace sw {

class Svf {
public:
    enum class Mode { Bell, LowShelf, HighShelf, LowPass, HighPass, Notch, BandPass };

    // set coefficients immediately
    void setup(Mode mode, double fc, double fs, double q, double gainDb) {
        compute(mode, fc, fs, q, gainDb, g_, k_, m0_, m1_, m2_);
        remaining_ = 0;
        updateA();
    }

    // move linearly from the current coefficients to the new ones over n samples
    // (piecewise-linear coefficient trajectory: no stair-step zipper noise)
    void setupRamp(Mode mode, double fc, double fs, double q, double gainDb, int n) {
        if (n <= 1) { setup(mode, fc, fs, q, gainDb); return; }
        double g = 0, k = 1, m0 = 1, m1 = 0, m2 = 0;
        compute(mode, fc, fs, q, gainDb, g, k, m0, m1, m2);
        const double inv = 1.0 / n;
        dg_ = (g - g_) * inv; dk_ = (k - k_) * inv;
        dm0_ = (m0 - m0_) * inv; dm1_ = (m1 - m1_) * inv; dm2_ = (m2 - m2_) * inv;
        tg_ = g; tk_ = k; tm0_ = m0; tm1_ = m1; tm2_ = m2;
        remaining_ = n;
    }

    double process(double v0) {
        if (remaining_ > 0) {
            if (--remaining_ == 0) { g_ = tg_; k_ = tk_; m0_ = tm0_; m1_ = tm1_; m2_ = tm2_; }
            else { g_ += dg_; k_ += dk_; m0_ += dm0_; m1_ += dm1_; m2_ += dm2_; }
            updateA();
        }
        const double v3 = v0 - ic2_;
        const double v1 = a1_ * ic1_ + a2_ * v3;
        const double v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2 * v1 - ic1_;
        ic2_ = 2 * v2 - ic2_;
        return m0_ * v0 + m1_ * v1 + m2_ * v2;
    }

    void reset() { ic1_ = ic2_ = 0; }

private:
    static void compute(Mode mode, double fc, double fs, double q, double gainDb,
                        double& g, double& k, double& m0, double& m1, double& m2) {
        constexpr double kPi = 3.14159265358979323846;
        fc = std::fmin(fc, fs * 0.49);
        const double A = std::pow(10.0, gainDb / 40.0);
        g = std::tan(kPi * fc / fs);
        k = 1.0 / q;
        switch (mode) {
            case Mode::Bell:      k = 1.0 / (q * A); m0 = 1; m1 = k * (A * A - 1); m2 = 0; break;
            case Mode::LowShelf:  g /= std::sqrt(A); m0 = 1; m1 = k * (A - 1); m2 = A * A - 1; break;
            case Mode::HighShelf: g *= std::sqrt(A); m0 = A * A; m1 = k * (1 - A) * A; m2 = 1 - A * A; break;
            case Mode::LowPass:   m0 = 0; m1 = 0; m2 = 1; break;
            case Mode::HighPass:  m0 = 1; m1 = -k; m2 = -1; break;
            case Mode::Notch:     m0 = 1; m1 = -k; m2 = 0; break;   // input - k * band
            case Mode::BandPass:  m0 = 0; m1 = k; m2 = 0; break;    // unity gain at the centre
        }
    }
    void updateA() {
        a1_ = 1.0 / (1.0 + g_ * (g_ + k_));
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;
    }
    double g_ = 0, k_ = 1, m0_ = 1, m1_ = 0, m2_ = 0;
    double tg_ = 0, tk_ = 1, tm0_ = 1, tm1_ = 0, tm2_ = 0;
    double dg_ = 0, dk_ = 0, dm0_ = 0, dm1_ = 0, dm2_ = 0;
    int remaining_ = 0;
    double a1_ = 1, a2_ = 0, a3_ = 0;
    double ic1_ = 0, ic2_ = 0;
};

// First-order TPT section (used for odd-order HPF, e.g. 18 dB/oct = 1st + 2nd order)
class OnePole {
public:
    enum class Mode { LowPass, HighPass };
    void setup(Mode mode, double fc, double fs) {
        G_ = gain(fc, fs);
        mode_ = mode;
        remaining_ = 0;
    }
    // linear move of the coefficient over n samples (see Svf::setupRamp)
    void setupRamp(Mode mode, double fc, double fs, int n) {
        if (n <= 1) { setup(mode, fc, fs); return; }
        mode_ = mode;
        tG_ = gain(fc, fs);
        dG_ = (tG_ - G_) / n;
        remaining_ = n;
    }
    double process(double x) {
        if (remaining_ > 0) G_ = --remaining_ == 0 ? tG_ : G_ + dG_;
        const double v = (x - s_) * G_;
        const double lp = v + s_;
        s_ = lp + v;
        return mode_ == Mode::LowPass ? lp : x - lp;
    }
    void reset() { s_ = 0; }

private:
    static double gain(double fc, double fs) {
        constexpr double kPi = 3.14159265358979323846;
        const double g = std::tan(kPi * std::fmin(fc, fs * 0.49) / fs);
        return g / (1.0 + g);
    }
    double G_ = 0, tG_ = 0, dG_ = 0, s_ = 0;
    int remaining_ = 0;
    Mode mode_ = Mode::LowPass;
};

}  // namespace sw
