#include "cr05/cr05.hpp"
#include <algorithm>
#include <cmath>

namespace sw::cr05 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"cr05.action",  "Action",     0, 2, 0,  Curve::Step, 1, {0, 1, 2}, "", {"Stop", "Start", "Spin back"}},
        {"cr05.stop",    "Stop time",  0, 5, 3,  Curve::Step, 1, {0, 1, 2, 3, 4, 5}, "", {"1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars"}},
        {"cr05.start",   "Start time", 0, 5, 1,  Curve::Step, 1, {0, 1, 2, 3, 4, 5}, "", {"1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars"}},
        {"cr05.curve",   "Curve",      0, 2, 1,  Curve::Step, 1, {0, 1, 2}, "", {"Lin", "Exp", "Log"}},
        {"cr05.filter",  "Filter",     0, 1, 1,  Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"cr05.trigger", "Trigger",    0, 1, 0,  Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

double barFraction(int i) { static const double f[6] = {1.0 / 16, 1.0 / 8, 1.0 / 4, 1.0 / 2, 1.0, 2.0}; return f[std::clamp(i, 0, 5)]; }

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::setTransport(bool playing, double toBar) {
    if (!playing || toBar < 0.0) return;
    hostSeen_ = true; barBeat_ = std::fmod(4.0 - toBar + 4.0, 4.0);
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    const size_t sz = static_cast<size_t>(1) << 19; mask_ = sz - 1;
    for (auto& r : ring_) r.assign(sz, 0.0f);
    for (auto& f : lp_) f.reset();
    t_ = 0; state_ = Idle; s_ = 1.0; live_ = 0.0; barBeat_ = 0.0; trig_ = false; hostSeen_ = false;
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

double Processor::warp(double u) const {
    const int c = static_cast<int>(target_[Curve_] + 0.5);
    return c == 1 ? u * u : (c == 2 ? std::sqrt(u) : u);
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const double bpm = bpm_ > 0.0 ? bpm_ : 120.0, barSamples = 4.0 * 60.0 / bpm * fs_, dBeat = bpm / 60.0 / fs_;
    for (int i = 0; i < n; ++i) {
        double dry[2] = {0, 0}; for (int c = 0; c < nch; ++c) dry[c] = ch[c][i];
        for (int c = 0; c < nch; ++c) ring_[static_cast<size_t>(c)][static_cast<size_t>(t_) & mask_] = static_cast<float>(dry[c]);
        if (nch == 1) ring_[1][static_cast<size_t>(t_) & mask_] = static_cast<float>(dry[0]);
        barBeat_ += dBeat; if (barBeat_ >= 4.0) barBeat_ -= 4.0;
        // the trigger
        const bool on = target_[Trigger] > 0.5;
        if (on && !trig_) {
            action_ = static_cast<int>(target_[Action] + 0.5);
            const double dur = barFraction(static_cast<int>(action_ == Start ? target_[StartTime] + 0.5 : target_[StopTime] + 0.5)) * barSamples;
            durSamples_ = std::max(64.0, dur); waitLeft_ = 0.0;
            if (hostSeen_ && action_ != Start) { const double toBar = (4.0 - barBeat_) / 4.0 * barSamples; waitLeft_ = toBar >= dur ? toBar - dur : toBar + barSamples - dur; }
            state_ = waitLeft_ > 0.0 ? Waiting : Running; u_ = 0.0; rp_ = static_cast<double>(t_); live_ = 0.0;
        }
        if (!on && trig_ && (state_ == Held || state_ == Running || state_ == Waiting)) { state_ = Resume; }
        trig_ = on;
        if (state_ == Waiting) { if (--waitLeft_ <= 0.0) { state_ = Running; u_ = 0.0; rp_ = static_cast<double>(t_); } }
        double out[2] = {dry[0], dry[1]};
        if (state_ == Running || state_ == Held || state_ == Resume) {
            if (state_ == Running) {
                u_ += 1.0 / durSamples_;
                if (u_ >= 1.0) {
                    u_ = 1.0;
                    if (action_ == Start) { state_ = Resume; live_ = 0.0; } else state_ = Held;
                }
                const double w = warp(std::min(1.0, u_));
                s_ = action_ == Stop ? 1.0 - w : (action_ == Start ? w : 1.0 - 3.0 * w);
            } else if (state_ == Held) { s_ = action_ == SpinBack ? -2.0 : 0.0; }
            // the tape
            rp_ += s_; rp_ = std::min(rp_, static_cast<double>(t_)); rp_ = std::max(rp_, static_cast<double>(t_ - static_cast<int64_t>(mask_) + 4));
            const double amp = state_ == Held ? 0.0 : std::min(1.0, 4.0 * std::abs(s_));
            if (target_[Filter] > 0.5 && (t_ & 15) == 0) for (auto& f : lp_) f.setup(Svf::Mode::LowPass, std::clamp(18000.0 * std::pow(std::abs(s_), 1.5), 300.0, 0.45 * fs_), fs_, 0.7071, 0.0);
            const int64_t i0 = static_cast<int64_t>(std::floor(rp_)); const double fr = rp_ - static_cast<double>(i0);
            for (int c = 0; c < nch; ++c) {
                const auto& r = ring_[static_cast<size_t>(c)];
                double w = (r[static_cast<size_t>(i0) & mask_] * (1.0 - fr) + r[static_cast<size_t>(i0 + 1) & mask_] * fr) * amp;
                if (target_[Filter] > 0.5) w = lp_[static_cast<size_t>(c)].process(w);
                out[c] = w;
            }
            if (state_ == Resume) {   // back to the live input over 30 ms (20 ms after a release)
                live_ = std::min(1.0, live_ + 1.0 / (0.03 * fs_));
                for (int c = 0; c < nch; ++c) out[c] = out[c] * (1.0 - live_) + dry[c] * live_;
                if (live_ >= 1.0) { state_ = Idle; s_ = 1.0; }
            }
        }
        for (int c = 0; c < nch; ++c) { double y = out[c]; if (!std::isfinite(y) || std::abs(y) < 1e-30) y = 0.0; ch[c][i] = static_cast<float>(y); }
        ++t_;
    }
}

}  // namespace sw::cr05
