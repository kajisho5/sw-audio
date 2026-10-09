#include "gt04/gt04.hpp"
#include <algorithm>
#include <cmath>
#include <complex>

namespace sw::gt04 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"gt04.gain",       "Gain",       0, 10, 5,   Curve::Lin, 1, {}, ""},
        {"gt04.drive",      "Drive",      0, 10, 0,   Curve::Lin, 1, {}, ""},
        {"gt04.master",     "Master",     0, 10, 5,   Curve::Lin, 1, {}, ""},
        {"gt04.low",        "Low",        0, 10, 5,   Curve::Lin, 1, {}, ""},
        {"gt04.lomid",      "Lo mid",     0, 10, 5,   Curve::Lin, 1, {}, ""},
        {"gt04.himid",      "Hi mid",     0, 10, 5,   Curve::Lin, 1, {}, ""},
        {"gt04.high",       "High",       0, 10, 5,   Curve::Lin, 1, {}, ""},
        {"gt04.midhz",      "Mid Hz",     250, 3000, 800, Curve::Step, 1, {250, 500, 800, 1500, 3000}, "Hz", {"250", "500", "800", "1.5k", "3k"}},
        {"gt04.di",         "DI",         0, 1, 1,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"gt04.diblend",    "DI blend",   0, 100, 50, Curve::Lin, 1, {}, "%"},
        {"gt04.evo.on",     "Phase align", 0, 1, 1,   Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        oversampleSpec("gt04.os"),
            unitSpec("gt04.unit"),
    };
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846;
double eqDb(double knob) { return (knob - 5.0) * 2.4; }
double masterDb(double k) { return (k - 5.0) * (k < 5.0 ? 12.0 : 2.0); }
}

double Processor::Chain::process(double x, double gPre, double hPre, double gDrv, double hDrv, double levelLin) {
    double v = hp20.process(x);
    v = pre.process(0, v, gPre, 0.1, hPre) * levelLin;
    v = high.process(himid.process(lomid.process(low.process(v))));
    const double lo = split_lo.process(v), hi = split_hi.process(v);
    const double loD = loOs.process(lo, [](double u) { return u; });   // same delay as the shaped branch
    const double dist = drv.process(0, hi, gDrv, 0.15, hDrv) * (1.0 + (1.0 - hDrv / 8.0) * 2.0);
    double y = loD + dist;
    y = cabLp.process(cabBell.process(cabHp.process(y)));
    return y;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::updateStatic() {
    for (auto& c : chain_) {
        c.hp20.setup(Svf::Mode::HighPass, 20.0, fs_, 0.70710678, 0);
        c.cabHp.setup(Svf::Mode::HighPass, 35.0, fs_, 0.70710678, 0);
        c.cabBell.setup(Svf::Mode::Bell, 90.0, fs_, 1.0, 2.0);
        c.cabLp.setup(Svf::Mode::LowPass, std::min(6000.0, 0.45 * fs_), fs_);
        c.split_lo.setup(Svf::Mode::LowPass, 150.0, fs_); c.split_hi.setup(Svf::Mode::HighPass, 150.0, fs_);
    }
}

// Unit A / B / C: chain 0 is the left channel, chain 1 the right one, chain 2 the copy the Phase align probe measures (it follows the left)
void Processor::applyUnit() {
    const int unit = static_cast<int>(target_[Unit]);
    for (size_t k = 0; k < chain_.size(); ++k) {
        const int ch = k == 1 ? 1 : 0;
        chain_[k].pre.setOnsetDb(0, sw::Unit::satDb(unit, ch, 0)); chain_[k].drv.setOnsetDb(0, sw::Unit::satDb(unit, ch, 1));
    }
}

void Processor::updateEq() {
    const double mid = target_[MidHz];
    const int unit = static_cast<int>(target_[Unit]);
    for (size_t k = 0; k < chain_.size(); ++k) {
        auto& c = chain_[k];
        const int ch = k == 1 ? 1 : 0;
        auto fm = [&](int slot, double f) { return std::min(f * sw::Unit::freqMul(unit, ch, slot), 0.45 * fs_); };
        c.low.setup(Svf::Mode::LowShelf, fm(0, 80.0), fs_, 0.70710678, eqDb(target_[Low]));
        c.lomid.setup(Svf::Mode::Bell, fm(1, std::max(100.0, mid * 0.5)), fs_, 1.0, eqDb(target_[LoMid]));
        c.himid.setup(Svf::Mode::Bell, fm(2, mid), fs_, 1.0, eqDb(target_[HiMid]));
        c.high.setup(Svf::Mode::HighShelf, fm(3, 3500.0), fs_, 0.70710678, eqDb(target_[High]));
    }
}

// the phase of the amp path at 150 Hz, from a small probe tone through the copy of the chain; the DI path gets the delay that has the same phase
void Processor::measure() {
    if (target_[PhaseAlign] < 0.5) { tau_ = 0.0; apA_ = 0.0; delayN_ = 0; return; }
    Chain& c = chain_[2];
    c = chain_[0];   // same settings; the probe starts from the current state, which only affects the warm-up
    c.pre.prepare(fs_); c.drv.prepare(fs_); c.loOs.reset();
    for (Svf* f : {&c.hp20, &c.low, &c.lomid, &c.himid, &c.high, &c.cabHp, &c.cabBell}) f->reset();
    for (Svf* f : {&c.split_lo.a, &c.split_lo.b, &c.split_hi.a, &c.split_hi.b, &c.cabLp.a, &c.cabLp.b}) f->reset();
    const double f0 = 150.0;
    const int N = static_cast<int>(std::lround(8.0 * fs_ / f0)), warm = static_cast<int>(0.1 * fs_);
    std::complex<double> a = 0, b = 0;
    const double gPre = 1.0 + target_[Gain] * 0.5, lvl = std::pow(10.0, (target_[Gain] - 5.0) * 1.2 / 20.0), gDrv = 1.0 + target_[Drive] * 2.4, hDrv = 8.0 - 0.68 * target_[Drive];
    for (int i = 0; i < warm + N; ++i) {
        const double x = 1e-3 * std::sin(2.0 * kPi * f0 * i / fs_);
        const double y = c.process(x, gPre, 2.5, gDrv, hDrv, lvl);
        if (i >= warm) { const std::complex<double> e = std::exp(std::complex<double>(0, -2.0 * kPi * f0 * i / fs_)); a += y * e; b += x * e; }
    }
    const double phaseAmp = std::arg(a / b);
    // the DI path already has the crossover all-pass: only what is left over is made up with a delay
    Lr4 lo, hi; lo.setup(Svf::Mode::LowPass, 150.0, fs_); hi.setup(Svf::Mode::HighPass, 150.0, fs_);
    Svf hp35; hp35.setup(Svf::Mode::HighPass, 35.0, fs_, 0.70710678, 0);
    std::complex<double> c1 = 0, c2 = 0;
    for (int i = 0; i < warm + N; ++i) {
        const double x = std::sin(2.0 * kPi * f0 * i / fs_);
        const double xh = hp35.process(x);
        const double y = lo.process(xh) + hi.process(xh);
        if (i >= warm) { const std::complex<double> e = std::exp(std::complex<double>(0, -2.0 * kPi * f0 * i / fs_)); c1 += y * e; c2 += x * e; }
    }
    const double phase = std::remainder(phaseAmp - std::arg(c1 / c2), 2.0 * kPi);
    tau_ = std::clamp(-phase / (2.0 * kPi * f0 / fs_), 0.0, 100.0);
    delayN_ = tau_ < 0.5 ? 0 : static_cast<int>(std::floor(tau_ - 0.5));
    const double frac = tau_ - delayN_;   // 0.5 .. 1.5 (or < 0.5 when there is no integer delay)
    apA_ = frac > 1e-3 ? (1.0 - frac) / (1.0 + frac) : 0.0;
}

void Processor::applyOversample(int factor) {
    for (auto& c : chain_) { c.pre.setOversample(factor); c.drv.setOversample(factor); c.loOs.setFactor(factor); }
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& c : chain_) { c = Chain{}; c.pre.prepare(fs_); c.drv.prepare(fs_); }
    applyOversample(static_cast<int>(target_[Oversample]));
    applyUnit();
    updateStatic(); updateEq();
    for (auto& d : dline_) d.assign(128, 0.0);
    for (auto& l : diLo_) l.setup(Svf::Mode::LowPass, 150.0, fs_);
    for (auto& l : diHi_) l.setup(Svf::Mode::HighPass, 150.0, fs_);
    for (auto& f : diHp_) f.setup(Svf::Mode::HighPass, 35.0, fs_, 0.70710678, 0);
    dpos_ = 0; apX_ = {}; apY_ = {};
    master_.reset(fs_, 20.0, std::pow(10.0, masterDb(target_[Master]) / 20.0));
    blend_.reset(fs_, 20.0, target_[DiBlend] * 0.01);
    measure();
    prepared_ = true; eqDirty_ = false;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (!prepared_) return;
    switch (id) {
        case Master: master_.setTarget(std::pow(10.0, masterDb(v) / 20.0)); break;
        case DiBlend: blend_.setTarget(v * 0.01); break;
        case Low: case LoMid: case HiMid: case High: case MidHz: case Gain: case Drive: case PhaseAlign: eqDirty_ = true; break;
        case Oversample: applyOversample(static_cast<int>(v)); eqDirty_ = true; break;
        case Unit: applyUnit(); eqDirty_ = true; break;   // the half-bands' delay changes: measure the phase again
        default: break;
    }
}

void Processor::snapToTargets() {
    if (!prepared_) return;
    master_.skip(1 << 30); blend_.skip(1 << 30);
    updateEq(); measure(); eqDirty_ = false;
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    if (eqDirty_) { updateEq(); measure(); eqDirty_ = false; }
    const double gPre = 1.0 + target_[Gain] * 0.5, lvl = std::pow(10.0, (target_[Gain] - 5.0) * 1.2 / 20.0), gDrv = 1.0 + target_[Drive] * 2.4, hDrv = 8.0 - 0.68 * target_[Drive];
    const bool di = target_[Di] > 0.5, align = target_[PhaseAlign] > 0.5;
    for (int i = 0; i < n; ++i) {
        const double m = master_.next(), b = blend_.next();
        for (int c = 0; c < nch; ++c) {
            const double x = ch[c][i];
            double y = chain_[static_cast<size_t>(c)].process(x, gPre, 2.5, gDrv, hDrv, lvl) * m;
            if (di && b > 0.0) {
                auto& d = dline_[static_cast<size_t>(c)];
                double xi = x;
                if (align) { const double xh = diHp_[static_cast<size_t>(c)].process(x); xi = diLo_[static_cast<size_t>(c)].process(xh) + diHi_[static_cast<size_t>(c)].process(xh); }
                d[static_cast<size_t>(dpos_)] = xi;
                const double dl = d[static_cast<size_t>((dpos_ + 128 - delayN_) % 128)];
                const size_t k = static_cast<size_t>(c);
                const double ap = apA_ * dl + apX_[k] - apA_ * apY_[k];   // first-order all-pass: delay of (1 - a) / (1 + a) samples at low frequencies
                apX_[k] = dl; apY_[k] = ap;
                y = (1.0 - b) * y + b * (apA_ != 0.0 ? ap : dl);
            }
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
        dpos_ = (dpos_ + 1) % 128;
    }
}

}  // namespace sw::gt04
