#include "eq04/eq04.hpp"
#include <algorithm>
#include <cmath>

namespace sw::eq04 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"eq04.hpf",       "HPF",      0, 300, 0,        Curve::Step, 1, {0, 50, 80, 160, 300}, "Hz", {"Off", "50 Hz", "80 Hz", "160 Hz", "300 Hz"}},
        {"eq04.low.freq",  "Low Hz",   35, 220, 60,      Curve::Step, 1, {35, 60, 110, 220}, "Hz"},
        {"eq04.low.gain",  "Low",      -16, 16, 0,       Curve::Lin,  1, {}, "dB"},
        {"eq04.mid.freq",  "Mid kHz",  360, 7200, 1600,  Curve::Step, 1, {360, 700, 1600, 3200, 4800, 7200}, "Hz"},
        {"eq04.mid.gain",  "Mid",      -18, 18, 0,       Curve::Lin,  1, {}, "dB"},
        {"eq04.high.freq", "High kHz", 10000, 16000, 12000, Curve::Step, 1, {10000, 12000, 16000}, "Hz"},
        {"eq04.high.gain", "High",     -16, 16, 0,       Curve::Lin,  1, {}, "dB"},
        {"eq04.drive",     "Drive",    0, 10, 2,         Curve::Lin,  1, {}, ""},
        {"eq04.out",       "Output",   -10, 10, 0,       Curve::Lin,  1, {}, "dB"},
        {"eq04.evo.on",    "Iron",     0, 1, 1,          Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

namespace {
constexpr int kControl = 16;
constexpr double kLowQ = 1.0;  // inductor bump: about +1 dB below the corner at full boost (measured +0.95 dB)
// iron: saturation drive per band at full boost (design values; lows saturate most)
constexpr double kIronLow = 6.0, kIronMid = 3.6, kIronHigh = 2.1;
double ironResidual(double u, double a) { return a < 1e-3 ? 0.0 : std::tanh(a * u) / a - u; }
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (LinearSmoother* s : {&hpfF_, &lowF_, &lowG_, &midF_, &midG_, &highF_, &highG_}) s->reset(fs_, 20.0, 0.0);
    hpfOn_.reset(fs_, 10.0, 0.0);
    iron_.reset(fs_, 10.0, 0.0);
    for (auto& c : ch_) c = Ch{};
    drive_.prepare(fs_, target_[Drive]);
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
    snapToTargets();
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case Hpf: hpfOn_.setTarget(v > 0 ? 1.0 : 0.0); if (v > 0) hpfF_.setTarget(std::log(v)); break;
        case LowFreq: lowF_.setTarget(std::log(v)); break;
        case LowGain: lowG_.setTarget(v); break;
        case MidFreq: midF_.setTarget(std::log(v)); break;
        case MidGain: midG_.setTarget(v); break;
        case HighFreq: highF_.setTarget(std::log(v)); break;
        case HighGain: highG_.setTarget(v); break;
        case Drive: drive_.set(v); break;
        case Iron: iron_.setTarget(v); break;
        default: break;
    }
}

void Processor::snapToTargets() {
    for (LinearSmoother* s : {&hpfF_, &hpfOn_, &lowF_, &lowG_, &midF_, &midG_, &highF_, &highG_, &iron_}) s->skip(1 << 30);
    if (hpfF_.current() == 0.0) hpfF_.reset(fs_, 20.0, std::log(50.0));
    drive_.snap();
    update(0);
}

void Processor::update(int ramp) {
    const double hf = std::exp(hpfF_.current() > 0 ? hpfF_.current() : std::log(50.0));
    const double highF = std::min(std::exp(highF_.current()), 0.45 * fs_);
    for (auto& c : ch_) {
        c.hp1.setupRamp(OnePole::Mode::HighPass, hf, fs_, ramp);
        c.hp2.setupRamp(Svf::Mode::HighPass, hf, fs_, 1.0, 0, ramp);  // with the 1st-order stage: 3rd-order Butterworth
        c.low.setupRamp(Svf::Mode::LowShelf, std::exp(lowF_.current()), fs_, kLowQ, lowG_.current(), ramp);
        c.mid.setupRamp(Svf::Mode::Bell, std::exp(midF_.current()), fs_, 0.9, midG_.current(), ramp);
        c.high.setupRamp(Svf::Mode::HighShelf, highF, fs_, 0.70710678, highG_.current(), ramp);
    }
}

void Processor::process(float** chans, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    for (int start = 0; start < n; start += kControl) {
        const int len = std::min(kControl, n - start);
        bool moving = false;
        for (LinearSmoother* s : {&hpfF_, &lowF_, &lowG_, &midF_, &midG_, &highF_, &highG_}) if (s->isSmoothing()) { s->skip(len); moving = true; }
        if (moving) update(len);
        // iron drive per band follows the boost (0 when cutting)
        const double aLow = kIronLow * std::max(0.0, lowG_.current()) / 16.0;
        const double aMid = kIronMid * std::max(0.0, midG_.current()) / 18.0;
        const double aHigh = kIronHigh * std::max(0.0, highG_.current()) / 16.0;
        for (int i = start; i < start + len; ++i) {
            drive_.tick();
            const double hpOn = hpfOn_.next(), iron = iron_.next();
            for (int c = 0; c < nch; ++c) {
                Ch& s = ch_[static_cast<size_t>(c)];
                double x = chans[c][i];
                const double hp = s.hp2.process(s.hp1.process(x));
                x = x + hpOn * (hp - x);
                const double lo = s.low.process(x), mi = s.mid.process(lo), hi = s.high.process(mi);
                double y = hi;
                if (iron > 0.0 && (aLow > 0 || aMid > 0 || aHigh > 0)) {
                    // the boosted part of each band, saturated at 2x and added back as a residual
                    double uL[2], uM[2], uH[2], r[2];
                    s.osLow.up(aLow > 0 ? lo - x : 0.0, uL);
                    s.osMid.up(aMid > 0 ? mi - lo : 0.0, uM);
                    s.osHigh.up(aHigh > 0 ? hi - mi : 0.0, uH);
                    for (int k = 0; k < 2; ++k) r[k] = ironResidual(uL[k], aLow) + ironResidual(uM[k], aMid) + ironResidual(uH[k], aHigh);
                    y += iron * s.osLow.down(r);
                }
                y = drive_.process(c, y);
                chans[c][i] = static_cast<float>(std::abs(y) < 1e-30 ? 0.0 : y);
            }
        }
    }
}

}  // namespace sw::eq04
