#include "lo02/lo02.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lo02 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"lo02.sub",     "Sub",      0, 10, 0,     Curve::Lin, 1, {}, ""},
            {"lo02.rangehz", "Range Hz", 30, 90, 45,   Curve::Step, 1, {30, 45, 60, 90}, "Hz", {"30", "45", "60", "90"}},
            {"lo02.tune",    "Tune",     -12, 12, 0,   Curve::Lin, 1, {}, "st"},
            {"lo02.punch",   "Punch",    0, 10, 0,     Curve::Lin, 1, {}, ""},
            {"lo02.dry",     "Dry",      0, 100, 100,  Curve::Lin, 1, {}, "%"},
        };
        v[Dry].minLabel = "Off"; v[Dry].maxLabel = "Full";
        return v;
    }();
    return s;
}

namespace {
double subGain(double sub) {   // Sub 0..10 -> the low end rises by 0..+12 dB (sub and band uncorrelated); 0 is off
    if (sub <= 0.0) return 0.0;
    const double r = std::pow(10.0, 12.0 * sub / 10.0 / 20.0);
    return std::sqrt(std::max(0.0, r * r - 1.0));
}
constexpr double kMinLevel = 5e-4, kFullLevel = 1e-3;   // below -66 dBFS the band is not tracked
double wrap(double x) { return x - std::floor(x + 0.5); }
// how far the detection chain (LR4 low-pass at fc, 2nd-order high-pass at 20 Hz) lags a sine of frequency f, in cycles (analog prototype at the warped frequency)
double chainLagCycles(double f, double fc, double fs) {
    const double pi = 3.14159265358979323846, w = std::tan(pi * f / fs);
    const double r1 = w / std::tan(pi * fc / fs), r2 = w / std::tan(pi * 20.0 / fs);
    const double th1 = std::atan2(1.4142135623730951 * r1, 1.0 - r1 * r1), th2 = std::atan2(1.4142135623730951 * r2, 1.0 - r2 * r2);
    return wrap((2.0 * th1 + th2 - pi) / (2.0 * pi));   // phase = -2 th1 + pi - th2 (a lag when positive after the sign flip)
}
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::updateFilters() { lp_.setup(2.0 * target_[RangeHz], fs_); }

void Processor::resetTracker() {
    lastFire_ = -1; period_ = 0; armed_ = false; flip_ = 0; rejects_ = 0; quiet_ = 0;
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    hp_.setup(Svf::Mode::HighPass, 20.0, fs_, 0.70710678, 0);
    updateFilters();
    time_ = 0; phase_ = 0; prev_ = 0; env_ = fast_ = slow_ = trans_ = 0;
    relDec_ = std::exp(-1.0 / (0.12 * fs_)); fastDec_ = std::exp(-1.0 / (0.01 * fs_)); transDec_ = std::exp(-1.0 / (0.04 * fs_));
    resetTracker();
    sub_.reset(fs_, 20.0, subGain(target_[Sub]));
    dry_.reset(fs_, 20.0, target_[Dry] * 0.01);
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    if (id == Tune) { v = std::round(v); tuneRatio_ = std::pow(2.0, v / 12.0); }
    target_[static_cast<size_t>(id)] = v;
    if (id == Sub) sub_.setTarget(subGain(v));
    if (id == Dry) dry_.setTarget(v * 0.01);
    if (id == RangeHz && prepared_) updateFilters();
}

void Processor::snapToTargets() {
    if (!prepared_) return;
    sub_.skip(1 << 30); dry_.skip(1 << 30);
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const double pMin = fs_ / (2.0 * target_[RangeHz] * 1.5), pMax = fs_ / 15.0;
    const double punch = target_[Punch] * 0.1, twoPi = 6.283185307179586;
    const bool lock = target_[Tune] == 0.0;
    for (int i = 0; i < n; ++i) {
        const double g = sub_.next(), dry = dry_.next();
        double m = 0.0;
        for (int c = 0; c < nch; ++c) m += ch[c][i];
        m /= nch;
        const double b = lp_.process(hp_.process(m));
        const double ab = std::abs(b);
        env_ = std::max(ab, env_ * relDec_);
        fast_ = std::max(ab, fast_ * fastDec_);
        slow_ += (ab - slow_) * (1.0 / (0.15 * fs_));
        time_ += 1.0;
        const double gate = std::clamp((env_ - kMinLevel) * (1.0 / (kFullLevel - kMinLevel)), 0.0, 1.0);
        if (env_ < kMinLevel) { if (++quiet_ > 0.2 * fs_) resetTracker(); } else quiet_ = 0;
        // Schmitt trigger: armed below -10 % of the level, fires at the next rise through zero
        if (b < -0.1 * env_ && env_ >= kMinLevel) armed_ = true;
        if (armed_ && b >= 0.0) {
            armed_ = false;
            const double tf = time_ - 1.0 + (b > prev_ ? -prev_ / (b - prev_) : 0.0);
            bool accept = true;
            if (lastFire_ >= 0.0) {
                const double p = tf - lastFire_;
                if (p < pMin || p > pMax) accept = false;
                else if (period_ > 0.0 && p < 0.6 * period_ && rejects_ < 2) { accept = false; ++rejects_; }
                else {
                    period_ = (period_ > 0.0 && std::abs(p / period_ - 1.0) < 0.3) ? 0.7 * period_ + 0.3 * p : p;
                    rejects_ = 0;
                }
            }
            if (accept) {
                flip_ ^= 1;
                const double target = flip_ ? 0.0 : 0.5;
                const double lag = period_ > 0.0 ? chainLagCycles(fs_ / period_, 2.0 * target_[RangeHz], fs_) * period_ : 0.0;   // samples: b crosses later than the bass
                const double ahead = (period_ > 0.0 ? tuneRatio_ / (2.0 * period_) : 0.0) * (time_ - tf + lag);
                if (lock && period_ > 0.0) phase_ += 0.5 * wrap(target - (phase_ - ahead));
                else if (period_ <= 0.0) phase_ = target + ahead;
                lastFire_ = tf;
            }
        }
        prev_ = b;
        if (period_ > 0.0) { phase_ += tuneRatio_ / (2.0 * period_); phase_ -= std::floor(phase_); }
        // Punch: a decaying boost after a level jump (the fast follower runs well above the slow one)
        const double ratio = fast_ / (slow_ + 1e-9);
        trans_ = std::max(std::clamp((ratio - 1.8) / 1.2, 0.0, 1.0), trans_ * transDec_);
        const double sub = g > 0.0 && period_ > 0.0 ? std::sin(twoPi * phase_) * g * env_ * gate * (1.0 + punch * trans_) : 0.0;
        for (int c = 0; c < nch; ++c) {
            double y = dry * ch[c][i] + sub;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::lo02
