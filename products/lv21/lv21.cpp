#include "lv21/lv21.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv21 {
namespace { constexpr double kPi = 3.14159265358979323846; }

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv21.signal", "Signal",     0, 4, 0,     Curve::Step, 1, {0, 1, 2, 3, 4}, "", {"Sine", "Pink", "White", "Sweep", "Polarity"}},
        {"lv21.freq",   "Freq",       20, 20000, 1000, Curve::Log, 1, {}, "Hz"},
        {"lv21.level",  "Level",      -60, 0, -20, Curve::Lin, 1, {}, "dBFS"},
        {"lv21.sweep",  "Sweep time", 1, 60, 10,   Curve::Log, 1, {}, "s"},
        {"lv21.left",   "Left",       0, 1, 1,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"lv21.right",  "Right",      0, 1, 1,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate; phase_ = sweepPhase_ = sweepT_ = env_ = elapsed_ = pulseClock_ = 0; pink_.fill(0.0); armed_ = on_ = false; prepared_ = true;
}
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); }
bool Processor::outputOn() { if (!armed_ || !prepared_) return false; if (!on_) { on_ = true; elapsed_ = 0; env_ = 0; } return true; }

double Processor::next(int signal) {
    const double lvl = std::pow(10.0, target_[Level] / 20.0), sqrt2 = 1.41421356237;
    switch (signal) {
        case Sine: { phase_ += 2 * kPi * std::min(target_[Freq], fs_ * 0.45) / fs_; if (phase_ > 2 * kPi) phase_ -= 2 * kPi; return lvl * sqrt2 * std::sin(phase_); }
        case White: { const double u = (static_cast<double>(rng_() >> 8) + 0.5) / 16777216.0; const double v = (static_cast<double>(rng_() >> 8) + 0.5) / 16777216.0; return lvl * std::sqrt(-2 * std::log(u)) * std::cos(2 * kPi * v); }
        case Pink: {   // Paul Kellet's refined filter (about -0.05 dB), then scaled so that the RMS is the Level
            const double u = (static_cast<double>(rng_() >> 8) + 0.5) / 16777216.0, v = (static_cast<double>(rng_() >> 8) + 0.5) / 16777216.0, w = std::sqrt(-2 * std::log(u)) * std::cos(2 * kPi * v);
            pink_[0] = 0.99886 * pink_[0] + w * 0.0555179; pink_[1] = 0.99332 * pink_[1] + w * 0.0750759; pink_[2] = 0.96900 * pink_[2] + w * 0.1538520; pink_[3] = 0.86650 * pink_[3] + w * 0.3104856;
            pink_[4] = 0.55000 * pink_[4] + w * 0.5329522; pink_[5] = -0.7616 * pink_[5] - w * 0.0168980;
            const double p = pink_[0] + pink_[1] + pink_[2] + pink_[3] + pink_[4] + pink_[5] + pink_[6] + w * 0.5362; pink_[6] = w * 0.115926;
            return lvl * p * 0.326917;   // the filter's RMS for unit white noise is 3.0589 (measured over 4 million samples)
        }
        case Sweep: {   // log sweep 20 Hz .. 20 kHz
            const double T = target_[SweepTime], f = 20.0 * std::pow(1000.0, sweepT_ / T);
            sweepPhase_ += 2 * kPi * std::min(f, fs_ * 0.45) / fs_; if (sweepPhase_ > 2 * kPi) sweepPhase_ -= 2 * kPi;
            sweepT_ += 1.0 / fs_; if (sweepT_ >= T) sweepT_ = 0.0;
            return lvl * sqrt2 * std::sin(sweepPhase_);
        }
        default: {   // Polarity pulse
            const double idx = pulseClock_; pulseClock_ += 1.0; if (pulseClock_ >= 0.5 * fs_) pulseClock_ = 0.0;
            return idx < 0.0001 * fs_ ? lvl : 0.0;
        }
    }
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    if (!on_ && env_ <= 0.0) return;   // untouched
    const int nc = std::min(numCh, 2), sig = static_cast<int>(target_[Signal] + 0.5);
    const bool use[2] = {target_[Left] > 0.5, target_[Right] > 0.5};
    for (int i = 0; i < n; ++i) {
        if (on_) { elapsed_ += 1.0 / fs_; if (elapsed_ >= kAutoStopSeconds) { on_ = false; armed_ = false; } }
        const double goal = on_ ? 1.0 : 0.0, step = on_ ? 1.0 / (0.5 * fs_) : 1.0 / (0.02 * fs_);
        if (env_ < goal) env_ = std::min(goal, env_ + step); else if (env_ > goal) env_ = std::max(goal, env_ - step);
        const double g = next(sig);
        for (int c = 0; c < nc; ++c) if (use[c]) { const double y = (1.0 - env_) * ch[c][i] + env_ * g; const float o = static_cast<float>(y); ch[c][i] = std::abs(o) < 1e-30f ? 0.0f : o; }
    }
}

}  // namespace sw::lv21
