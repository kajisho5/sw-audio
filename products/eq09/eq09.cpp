#include "eq09/eq09.hpp"
#include <algorithm>
#include <cmath>

namespace sw::eq09 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"eq09.tilt",      "Tilt",       -6, 6, 0,       Curve::Lin,  1, {}, "dB"},
        {"eq09.pivot",     "Pivot Hz",   300, 4000, 1000, Curve::Step, 1, {300, 600, 1000, 2000, 4000}, "Hz", {"300 Hz", "600 Hz", "1.0 kHz", "2.0 kHz", "4.0 kHz"}},
        {"eq09.lowlift",   "Low lift",   0, 10, 0,       Curve::Lin,  1, {}, ""},
        {"eq09.air",       "Air",        0, 10, 0,       Curve::Lin,  1, {}, ""},
        {"eq09.out",       "Output",     -10, 10, 0,     Curve::Lin,  1, {}, "dB"},
        {"eq09.evo.on",    "Auto pivot", 0, 1, 0,        Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            unitSpec("eq09.unit"),
    };
    return s;
}

namespace { constexpr int kControl = 32; }

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    tilt_.reset(fs_, 20.0, target_[Tilt]);
    low_.reset(fs_, 20.0, target_[LowLift] * 0.6);
    air_.reset(fs_, 20.0, target_[Air] * 0.6);
    avgCoef_ = std::exp(-1.0 / (10.0 * fs_));                 // 10 s analysis window
    glideCoef_ = std::exp(-1.0 / (5.0 * fs_ / kControl));      // 5 s pivot glide (control rate)
    pivot_ = target_[PivotHz];
    ex_ = ed_ = 0; prev_[0] = prev_[1] = 0;
    for (auto& c : ch_) { c.tiltLo.reset(); c.tiltHi.reset(); c.low.reset(); c.air.reset(); }
    update(0);
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Tilt) tilt_.setTarget(v);
    else if (id == LowLift) low_.setTarget(v * 0.6);   // 0..10 -> 0..+6 dB
    else if (id == Air) air_.setTarget(v * 0.6);
    else if (id == PivotHz && target_[AutoPivot] < 0.5) pivot_ = v;
    else if (id == AutoPivot && v < 0.5) pivot_ = target_[PivotHz];
    else if (id == Unit) { unit_ = static_cast<int>(v); update(static_cast<int>(0.02 * fs_)); }   // the coefficients move over 20 ms
}

void Processor::snapToTargets() { tilt_.skip(1 << 30); low_.skip(1 << 30); air_.skip(1 << 30); update(0); }

void Processor::update(int ramp) {
    const double t = tilt_.current();
    for (int k = 0; k < 2; ++k) {   // each channel's parts have their own tolerance (Unit B / C)
        Ch& c = ch_[static_cast<size_t>(k)];
        const double pv = pivot_ * Unit::freqMul(unit_, k, 0);
        c.tiltLo.setupRamp(Svf::Mode::LowShelf, pv, fs_, 0.5, -t, ramp);
        c.tiltHi.setupRamp(Svf::Mode::HighShelf, pv, fs_, 0.5, t, ramp);
        c.low.setupRamp(Svf::Mode::LowShelf, 60.0 * Unit::freqMul(unit_, k, 1), fs_, 0.70710678, low_.current(), ramp);
        c.air.setupRamp(Svf::Mode::HighShelf, std::min(12000.0 * Unit::freqMul(unit_, k, 2), 0.45 * fs_), fs_, 0.70710678, air_.current(), ramp);
    }
}

void Processor::process(float** chans, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const bool autoPivot = target_[AutoPivot] > 0.5;
    for (int i = 0; i < n; ++i) {
        if (sinceUpdate_ == 0) {
            tilt_.skip(kControl); low_.skip(kControl); air_.skip(kControl);
            if (autoPivot && ex_ > 1e-9) {
                // RMS frequency of the material: E[d^2]/E[x^2] = 4 sin^2(w/2) for a sine
                const double ratio = std::clamp(ed_ / ex_, 0.0, 4.0);
                const double w = 2.0 * std::asin(std::sqrt(ratio) / 2.0);
                const double f = std::clamp(w * fs_ / (2.0 * 3.14159265358979323846), 300.0, 4000.0);
                pivot_ = std::exp(std::log(f) + glideCoef_ * (std::log(pivot_) - std::log(f)));
            }
            update(kControl);
        }
        sinceUpdate_ = (sinceUpdate_ + 1) % kControl;
        for (int c = 0; c < nch; ++c) {
            const double x = chans[c][i];
            if (autoPivot) {
                const double d = x - prev_[c];
                prev_[c] = x;
                ex_ = x * x + avgCoef_ * (ex_ - x * x);
                ed_ = d * d + avgCoef_ * (ed_ - d * d);
            }
            Ch& s = ch_[static_cast<size_t>(c)];
            double y = s.air.process(s.low.process(s.tiltHi.process(s.tiltLo.process(x))));
            if (std::abs(y) < 1e-30) y = 0.0;
            chans[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::eq09
