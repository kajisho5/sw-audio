#include "lv04/lv04.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv04 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"lv04.mode",     "Mode",      0, 1, 0,       Curve::Step, 1, {0, 1}, "", {"Zero", "True peak"}},
            {"lv04.ceiling",  "Ceiling",   -12, 0, -1,    Curve::Lin,  1, {}, "dBFS"},
            {"lv04.release",  "Release",   10, 1000, 50,  Curve::Log,  1, {}, "ms"},
            {"lv04.rmslimit", "RMS limit", -20, 0, -6,    Curve::Lin,  1, {}, "dB"},
            {"lv04.subsonic", "Subsonic",  20, 60, 30,    Curve::Log,  1, {}, "Hz"},
        };
        v[Subsonic].minLabel = "Off";
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

int Processor::latencySamples() const {
    if (target_[Mode] < 0.5) return 0;
    return static_cast<int>(std::lround(fs_ * 0.0015)) + TruePeakDetector::kTapsPerPhase;  // look-ahead + detector + margin
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    tp_ = target_[Mode] > 0.5;
    if (tp_) lim_.prepare(fs_, 2, static_cast<int>(std::lround(fs_ * 0.0015)), true, 4);
    msCoef_ = Ballistics::coef(fs_, 1000.0);  // 1 s RMS
    rms_.set(fs_, 200.0, 1000.0);
    rms_.reset(0.0);
    ms_ = 0; r_ = 1; inEvent_ = false;
    for (auto& f : sub_) f.reset();
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case Ceiling: case Release:
            ceil_ = std::pow(10.0, target_[Ceiling] / 20.0);
            relCoef_ = Ballistics::coef(fs_, target_[Release]);
            if (tp_) lim_.set(target_[Ceiling] - 0.02, target_[Release], 1.0);
            break;
        case Subsonic:
            subOn_ = v > sp.min * 1.0001;
            for (auto& f : sub_) f.setup(Svf::Mode::HighPass, v, fs_, 0.70710678, 0);
            break;
        default: break;  // Mode: next prepare (latency change); RmsLimit: read per sample
    }
}

void Processor::logLimiting(double gain) {
    const bool limiting = gain < 0.999;
    const double db = 20.0 * std::log10(std::max(gain, 1e-9));
    if (limiting && !inEvent_) {
        LimitEvent& e = log_[static_cast<size_t>(events_ % kLog)];
        e = LimitEvent{t_, 0, db};
        ++events_;
    }
    if (limiting) {
        LimitEvent& e = log_[static_cast<size_t>((events_ - 1) % kLog)];
        e.lengthSamples = t_ - e.startSample + 1;
        e.maxReductionDb = std::min(e.maxReductionDb, db);
    }
    inEvent_ = limiting;
}

LimitEvent Processor::event(int i) const {
    const long long first = events_ > kLog ? events_ - kLog : 0;
    return log_[static_cast<size_t>((first + i) % kLog)];
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const double rmsThr = target_[Ceiling] + target_[RmsLimit];
    for (int i = 0; i < n; ++i) {
        double peak = 0, msMax = 0;
        for (int c = 0; c < nch; ++c) {
            double x = ch[c][i];
            if (subOn_) x = sub_[static_cast<size_t>(c)].process(x);
            ch[c][i] = static_cast<float>(x);
            msMax = std::max(msMax, x * x);
        }
        // long-term RMS limit (ratio infinity, attack 200 ms / release 1 s)
        ms_ = msMax + msCoef_ * (ms_ - msMax);
        const double rmsDb = 10.0 * std::log10(std::max(ms_, 1e-20));
        const double g = std::pow(10.0, rms_.process(std::min(0.0, rmsThr - rmsDb)) / 20.0);
        for (int c = 0; c < nch; ++c) {
            ch[c][i] = static_cast<float>(ch[c][i] * g);
            peak = std::max(peak, static_cast<double>(std::abs(ch[c][i])));
        }
        if (!tp_) {  // Zero mode: instantaneous, linked; the gain is never above ceiling / peak
            const double gi = peak > ceil_ ? ceil_ / peak : 1.0;
            r_ = gi < r_ ? gi : gi + relCoef_ * (r_ - gi);
            for (int c = 0; c < nch; ++c) {
                double y = ch[c][i] * r_;
                if (std::abs(y) < 1e-30) y = 0.0;
                ch[c][i] = static_cast<float>(std::clamp(y, -ceil_, ceil_));
            }
            logLimiting(r_);
        }
        ++t_;
    }
    if (tp_) {
        lim_.process(ch, nch, n);
        // block-level logging in true-peak mode (gain of the last sample in the block)
        t_ -= n;
        for (int i = 0; i < n; ++i) { if (i == n - 1) logLimiting(std::pow(10.0, lim_.gainReductionDb() / 20.0)); ++t_; }
    }
}

}  // namespace sw::lv04
