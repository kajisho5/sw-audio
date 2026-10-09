#include "eq03/eq03.hpp"
#include <algorithm>
#include <cmath>

namespace sw::eq03 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"eq03.dip.freq",    "Dip kHz",  200, 3000, 1000, Curve::Step, 1, {200, 500, 1000, 1500, 2000, 3000}, "Hz"},
            {"eq03.dip.amount",  "Dip",      0, 10, 0,        Curve::Lin,  1, {}, ""},
            {"eq03.peak.freq",   "Peak kHz", 700, 5000, 2000, Curve::Step, 1, {700, 1000, 1500, 2000, 3000, 4000, 5000}, "Hz"},
            {"eq03.peak.amount", "Peak",     0, 10, 0,        Curve::Lin,  1, {}, ""},
            {"eq03.width",       "Width",    0.6, 2.5, 1.2,   Curve::Log,  1, {}, "Q"},
            {"eq03.drive",       "Drive",    0, 10, 2,        Curve::Lin,  1, {}, ""},
            {"eq03.out",         "Output",   -10, 10, 0,      Curve::Lin,  1, {}, "dB"},
            {"eq03.evo.on",      "Ride",     0, 1, 0,         Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            oversampleSpec("eq03.os"),
        };
        v[Width].reversed = true;  // Narrow (Q 2.5) ... Wide (Q 0.6)
        return v;
    }();
    return s;
}

namespace { constexpr int kControl = 16; constexpr double kRefDbfs = -18.0, kRideRangeDb = 12.0; }

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (LinearSmoother* s : {&dipF_, &dipG_, &peakF_, &peakG_, &width_}) s->reset(fs_, 20.0, 0.0);
    for (auto& f : dip_) f.reset();
    for (auto& f : peak_) f.reset();
    bandHp_.setup(Svf::Mode::HighPass, 200.0, fs_, 0.70710678, 0);
    bandLp_.setup(Svf::Mode::LowPass, 5000.0, fs_, 0.70710678, 0);
    att_ = std::exp(-1.0 / (0.050 * fs_)); rel_ = std::exp(-1.0 / (0.300 * fs_));
    ms_ = 0; rideMul_ = 1;
    drive_.prepare(fs_, target_[Drive]);
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
    snapToTargets();
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case DipFreq: dipF_.setTarget(std::log(v)); break;
        case Dip: dipG_.setTarget(-v); break;     // 0..10 -> 0..-10 dB
        case PeakFreq: peakF_.setTarget(std::log(v)); break;
        case Peak: peakG_.setTarget(v); break;    // 0..10 -> 0..+10 dB
        case Width: width_.setTarget(std::log(v)); break;
        case Drive: drive_.set(v); break;
        case Oversample: drive_.setOversample(static_cast<int>(v)); break;
        default: break;  // Output: sw::Shell, Ride: per sample
    }
}

void Processor::snapToTargets() {
    for (LinearSmoother* s : {&dipF_, &dipG_, &peakF_, &peakG_, &width_}) s->skip(1 << 30);
    drive_.snap();
    update(0);
}

void Processor::update(int ramp) {
    const double q = std::exp(width_.current());
    for (auto& f : dip_) f.setupRamp(Svf::Mode::Bell, std::exp(dipF_.current()), fs_, q, dipG_.current(), ramp);
    for (auto& f : peak_) f.setupRamp(Svf::Mode::Bell, std::exp(peakF_.current()), fs_, q, peakG_.current() * rideMul_, ramp);
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const bool ride = target_[Ride] > 0.5;
    for (int start = 0; start < n; start += kControl) {
        const int len = std::min(kControl, n - start);
        bool moving = false;
        for (LinearSmoother* s : {&dipF_, &dipG_, &peakF_, &peakG_, &width_}) if (s->isSmoothing()) { s->skip(len); moving = true; }
        // Ride: measure this block's band level, then set the peak for it
        if (ride) {
            for (int i = start; i < start + len; ++i) {
                double m = 0; for (int c = 0; c < nch; ++c) m += ch[c][i];
                const double b = bandLp_.process(bandHp_.process(m / nch)), p = b * b;
                ms_ = p + (p > ms_ ? att_ : rel_) * (ms_ - p);
            }
            const double lv = 10.0 * std::log10(std::max(ms_, 1e-20)) + 3.0103;  // RMS of a sine reads its dBFS RMS
            const double want = 1.0 - std::clamp((lv - kRefDbfs) / kRideRangeDb, 0.0, 1.0);
            if (std::abs(want - rideMul_) > 1e-4) { rideMul_ = want; moving = true; }
        } else if (rideMul_ != 1.0) { rideMul_ = 1.0; moving = true; }
        if (moving) update(len);
        for (int i = start; i < start + len; ++i) {
            drive_.tick();
            for (int c = 0; c < nch; ++c) {
                double y = peak_[static_cast<size_t>(c)].process(dip_[static_cast<size_t>(c)].process(ch[c][i]));
                y = drive_.process(c, y);
                ch[c][i] = static_cast<float>(std::abs(y) < 1e-30 ? 0.0 : y);
            }
        }
    }
}

}  // namespace sw::eq03
