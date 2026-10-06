#include "eq01/eq01.hpp"
#include <algorithm>
#include <cmath>

namespace sw::eq01 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"eq01.low.freq",    "Low Freq", 20, 400, 60,         Curve::Log,  1, {}, "Hz"},
            {"eq01.low.gain",    "Low Gain", -12, 12, 0,          Curve::Lin,  1, {}, "dB"},
            {"eq01.low.contour", "Contour",  0, 10, 0,            Curve::Lin,  1, {}, ""},
            {"eq01.air.freq",    "Air Freq", 2000, 20000, 10000,  Curve::Log,  1, {}, "Hz"},
            {"eq01.air.gain",    "Air Gain", -12, 12, 0,          Curve::Lin,  1, {}, "dB"},
            {"eq01.air.width",   "Width",    0.4, 2.0, 0.8,       Curve::Log,  1, {}, "Q"},
            {"eq01.out.drive",   "Drive",    0, 10, 2,            Curve::Lin,  1, {}, ""},
            {"eq01.out.level",   "Output",   -10, 10, 0,          Curve::Lin,  1, {}, "dB"},
            {"eq01.mode",        "Mode",     0, 1, 0,             Curve::Step, 1, {0, 1}, "", {"LR", "MS"}},
        };
        v[Width].reversed = true;  // Narrow (Q 2.0) ... Wide (Q 0.4)
        return v;
    }();
    return s;
}

namespace {
constexpr int kControl = 16;
double contourQ(double c) { return 0.70710678 * std::pow(3.0 / 0.70710678, c / 10.0); }  // 0 -> 0.707, 10 -> 3.0
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (LinearSmoother* s : {&lowF_, &lowG_, &contour_, &airF_, &airG_, &width_}) s->reset(fs_, 20.0, 0.0);
    ms_.reset(fs_, 10.0, 0.0);
    for (auto& f : low_) f.reset();
    for (auto& f : air_) f.reset();
    lowM_.reset(); airM_.reset();
    drive_.prepare(fs_, target_[Drive]);
    driveM_.prepare(fs_, target_[Drive]);
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
    snapToTargets();
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case LowFreq: lowF_.setTarget(std::log(v)); break;
        case LowGain: lowG_.setTarget(v); break;
        case Contour: contour_.setTarget(v); break;
        case AirFreq: airF_.setTarget(std::log(v)); break;
        case AirGain: airG_.setTarget(v); break;
        case Width: width_.setTarget(std::log(v)); break;
        case Drive: drive_.set(v); driveM_.set(v); break;
        case Mode: ms_.setTarget(v); break;
        default: break;  // Output: sw::Shell
    }
}

void Processor::snapToTargets() {
    for (LinearSmoother* s : {&lowF_, &lowG_, &contour_, &airF_, &airG_, &width_, &ms_}) s->skip(1 << 30);
    drive_.snap();
    driveM_.snap();
    update(0);
}

void Processor::update(int ramp) {
    const double lf = std::exp(lowF_.current()), af = std::min(std::exp(airF_.current()), 0.45 * fs_);
    for (auto& f : low_) f.setupRamp(Svf::Mode::LowShelf, lf, fs_, contourQ(contour_.current()), lowG_.current(), ramp);
    for (auto& f : air_) f.setupRamp(Svf::Mode::Bell, af, fs_, std::exp(width_.current()), airG_.current(), ramp);
    lowM_.setupRamp(Svf::Mode::LowShelf, lf, fs_, contourQ(contour_.current()), lowG_.current(), ramp);
    airM_.setupRamp(Svf::Mode::Bell, af, fs_, std::exp(width_.current()), airG_.current(), ramp);
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    for (int start = 0; start < n; start += kControl) {
        const int len = std::min(kControl, n - start);
        bool moving = false;
        for (LinearSmoother* s : {&lowF_, &lowG_, &contour_, &airF_, &airG_, &width_}) if (s->isSmoothing()) { s->skip(len); moving = true; }
        if (moving) update(len);
        for (int i = start; i < start + len; ++i) {
            drive_.tick();
            driveM_.tick();
            const double ms = ms_.next();
            if (nch == 2) {
                const double l = ch[0][i], r = ch[1][i];
                // LR: both channels through the EQ. MS: the mid through the EQ, the side untouched (crossfaded switch)
                const double m = 0.5 * (l + r), sd = 0.5 * (l - r);
                double yl = l, yr = r, ym = m;
                if (ms < 1.0) {
                    yl = drive_.process(0, air_[0].process(low_[0].process(l)));
                    yr = drive_.process(1, air_[1].process(low_[1].process(r)));
                }
                if (ms > 0.0) {
                    ym = driveM_.process(0, airM_.process(lowM_.process(m)));
                    const double ml = ym + sd, mr = ym - sd;
                    yl = ms >= 1.0 ? ml : yl + ms * (ml - yl);
                    yr = ms >= 1.0 ? mr : yr + ms * (mr - yr);
                }
                ch[0][i] = static_cast<float>(std::abs(yl) < 1e-30 ? 0.0 : yl);
                ch[1][i] = static_cast<float>(std::abs(yr) < 1e-30 ? 0.0 : yr);
            } else {
                const double y = drive_.process(0, air_[0].process(low_[0].process(ch[0][i])));
                ch[0][i] = static_cast<float>(std::abs(y) < 1e-30 ? 0.0 : y);
            }
        }
    }
}

}  // namespace sw::eq01
