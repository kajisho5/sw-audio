#include "md06/md06.hpp"
#include <algorithm>
#include <cmath>

namespace sw::md06 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"md06.shift",     "Shift",     -2000, 2000, 35, Curve::SymLog, 2000, {}, "Hz"},
        {"md06.direction", "Direction", 0, 2, 2,         Curve::Step, 1, {0, 1, 2}, "", {"Up", "Down", "Both"}},
        {"md06.ringmod",   "Ring mod",  0, 1, 0,         Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"md06.feedback",  "Feedback",  0, 100, 20,      Curve::Lin, 1, {}, "%"},
        {"md06.lfo",       "LFO",       0, 1, 0,         Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"md06.mix",       "Mix",       0, 100, 50,      Curve::Lin, 1, {}, "%"},
        {"md06.evo.on",    "Pitch track", 0, 1, 0,       Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846, kLimit = 4.0;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& h : hil_) h.reset();
    osc_ = lfoPh_ = 0.0; fbState_[0] = fbState_[1] = 0.0; f0Smooth_ = 0.0; lastShift_ = 0.0;
    pitch_.prepare(fs_);
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::snapToTargets() {}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const int dir = static_cast<int>(target_[Direction] + 0.5);
    double s = target_[Shift];
    if (dir == Up) s = std::abs(s); else if (dir == Down) s = -std::abs(s);
    const bool ring = target_[RingMod] > 0.5, lfo = target_[Lfo] > 0.5, follow = target_[PitchTrack] > 0.5;
    const double fb = target_[Feedback] * 0.01;
    const double glide = 1.0 - std::exp(-1.0 / (0.15 * fs_));
    for (int i = 0; i < n; ++i) {
        if (follow) {
            pitch_.push(nch > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i]);
            if (pitch_.voiced() && pitch_.f0() > 0.0) { if (f0Smooth_ <= 0.0) f0Smooth_ = pitch_.f0(); else f0Smooth_ += glide * (pitch_.f0() - f0Smooth_); }
        }
        double sh = s;
        if (follow && f0Smooth_ > 0.0) sh *= std::clamp(f0Smooth_ / kRefHz, 0.25, 8.0);
        if (lfo) { lfoPh_ += kLfoHz / fs_; if (lfoPh_ >= 1.0) lfoPh_ -= 1.0; sh *= std::sin(2.0 * kPi * lfoPh_); }
        sh = std::clamp(sh, -0.45 * fs_, 0.45 * fs_);
        lastShift_ = sh;
        osc_ += sh / fs_; osc_ -= std::floor(osc_);
        const double co = std::cos(2.0 * kPi * osc_), si = std::sin(2.0 * kPi * osc_);
        for (int c = 0; c < nch; ++c) {
            const double x = ch[c][i] + fb * kLimit * std::tanh(fbState_[static_cast<size_t>(c)] / kLimit);
            double I, Q; hil_[static_cast<size_t>(c)].process(x, I, Q);
            // q leads i by 90 degrees (q = - the Hilbert transform of i), so i cos + q sin is the upper sideband (the tests: +100 Hz moves 1 kHz up)
            const double y = ring ? I * co : I * co + Q * si;
            fbState_[static_cast<size_t>(c)] = y;
            double o = y; if (std::abs(o) < 1e-30) o = 0.0;
            ch[c][i] = static_cast<float>(o);
        }
    }
}

}  // namespace sw::md06
