#include "cs02/cs02.hpp"
#include <algorithm>
#include <cmath>

namespace sw::cs02 {

namespace { constexpr double kRefDbfs = -18.0; constexpr int kControl = 16; }

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"cs02.comp.ratio",   "Ratio",   1, 20, 1,       Curve::Log,  1, {}, ":1"},
            {"cs02.comp.thresh",  "Thresh",  -20, 10, 10,    Curve::Lin,  1, {}, "dB"},
            {"cs02.comp.release", "Release", 0.1, 4, 0.3,    Curve::Skew, 2, {}, "s"},
            {"cs02.gate.thresh",  "Gate",    -30, 10, -30,   Curve::Lin,  1, {}, "dB"},
            {"cs02.gate.range",   "Range",   0, 40, 0,       Curve::Lin,  1, {}, "dB"},
            {"cs02.hpf",          "HPF",     0, 350, 0,      Curve::Step, 1, {0, 40, 80, 160, 350}, "Hz", {"Off", "40 Hz", "80 Hz", "160 Hz", "350 Hz"}},
            {"cs02.lpf",          "LPF",     0, 12000, 0,    Curve::Step, 1, {0, 12000, 8000, 4000}, "Hz", {"Off", "12 kHz", "8 kHz", "4 kHz"}},
            {"cs02.eq.hf",        "HF",      -15, 15, 0,     Curve::Lin,  1, {}, "dB"},
            {"cs02.eq.hmf",       "HMF",     -15, 15, 0,     Curve::Lin,  1, {}, "dB"},
            {"cs02.eq.lmf",       "LMF",     -15, 15, 0,     Curve::Lin,  1, {}, "dB"},
            {"cs02.eq.lf",        "LF",      -15, 15, 0,     Curve::Lin,  1, {}, "dB"},
            {"cs02.route",        "Route",   0, 1, 0,        Curve::Step, 1, {0, 1}, "", {"Dyn to EQ", "EQ to Dyn"}},
            {"cs02.fader",        "Fader",   -100, 10, 0,    Curve::Fader, 1, {}, "dB"},
            {"cs02.link",         "Link",    0, 1, 1,        Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        };
        v[Ratio].maxLabel = "Max";      // rightmost = infinity
        v[Thresh].reversed = true;      // knob runs +10 .. -20 (spec)
        v[Fader].minLabel = "Off";
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (LinearSmoother* s : {&hpfF_, &lpfF_, &hf_, &hmf_, &lmf_, &lf_, &fader_}) s->reset(fs_, 20.0, 0.0);
    for (LinearSmoother* s : {&hpfOn_, &lpfOn_, &route_}) s->reset(fs_, 10.0, 0.0);
    for (auto& f : hp1_) f.reset();
    for (auto& f : hp2_) f.reset();
    for (auto& f : lp_) f.reset();
    chain_ = {};
    for (auto& c : chain_) { for (auto& g : c.dyn.gate) g.prepare(fs_); for (auto& d : c.dyn.det) d.set(fs_, LevelDetector::Mode::Rms); }
    atk_ = Ballistics::coef(fs_, 3.0);
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
    snapToTargets();
}

void Processor::updateDyn() {
    const double ratio = target_[Ratio] >= specs()[Ratio].max ? GainComputer::kInfinity : target_[Ratio];
    gc_.set(target_[Thresh] + kRefDbfs, ratio, 0.0);
    rel_ = Ballistics::coef(fs_, target_[Release] * 1000.0);
    for (auto& c : chain_)
        for (auto& g : c.dyn.gate)
            g.set(GateEngine::Mode::Gate, target_[GateThresh] + kRefDbfs, -target_[GateRange], 0.1, 20.0, 100.0);
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case Ratio: case Thresh: case Release: case GateThresh: case GateRange: updateDyn(); break;
        case Hpf: hpfOn_.setTarget(v > 0 ? 1.0 : 0.0); if (v > 0) hpfF_.setTarget(std::log(v)); break;
        case Lpf: lpfOn_.setTarget(v > 0 ? 1.0 : 0.0); if (v > 0) lpfF_.setTarget(std::log(v)); break;
        case Hf: hf_.setTarget(v); break;
        case Hmf: hmf_.setTarget(v); break;
        case Lmf: lmf_.setTarget(v); break;
        case Lf: lf_.setTarget(v); break;
        case Route: route_.setTarget(v); break;
        case Fader: fader_.setTarget(v <= sp.min ? 0.0 : std::pow(10.0, v / 20.0)); break;
        default: break;
    }
}

void Processor::snapToTargets() {
    for (LinearSmoother* s : {&hpfF_, &hpfOn_, &lpfF_, &lpfOn_, &hf_, &hmf_, &lmf_, &lf_, &fader_, &route_}) s->skip(1 << 30);
    if (hpfF_.current() == 0.0) hpfF_.reset(fs_, 20.0, std::log(80.0));
    if (lpfF_.current() == 0.0) lpfF_.reset(fs_, 20.0, std::log(12000.0));
    updateFilters(0);
}

void Processor::updateFilters(int ramp) {
    const double hf = std::exp(hpfF_.current()), lpf = std::min(std::exp(lpfF_.current()), 0.45 * fs_);
    for (int c = 0; c < 2; ++c) {
        hp1_[static_cast<size_t>(c)].setupRamp(OnePole::Mode::HighPass, hf, fs_, ramp);
        hp2_[static_cast<size_t>(c)].setupRamp(Svf::Mode::HighPass, hf, fs_, 1.0, 0, ramp);
        lp_[static_cast<size_t>(c)].setupRamp(Svf::Mode::LowPass, lpf, fs_, 0.70710678, 0, ramp);
    }
    for (auto& ch : chain_)
        for (auto& e : ch.eq) {
            e.lf.setupRamp(Svf::Mode::LowShelf, 100.0, fs_, 0.70710678, lf_.current(), ramp);
            e.lmf.setupRamp(Svf::Mode::Bell, 600.0, fs_, 1.0, lmf_.current(), ramp);
            e.hmf.setupRamp(Svf::Mode::Bell, 3000.0, fs_, 1.0, hmf_.current(), ramp);
            e.hf.setupRamp(Svf::Mode::HighShelf, std::min(10000.0, 0.45 * fs_), fs_, 0.70710678, hf_.current(), ramp);
        }
}

void Processor::dynSample(Dyn& d, double* x, int nch) {
    const bool link = target_[Link] > 0.5 && nch == 2;
    // gate (peak key per channel, linked like the compressor)
    double gk[2] = {std::abs(x[0]), nch > 1 ? std::abs(x[1]) : 0.0};
    if (link) gk[0] = gk[1] = std::max(gk[0], gk[1]);
    for (int k = 0; k < nch; ++k) x[k] *= d.gate[static_cast<size_t>(k)].process(gk[k]);
    // VCA feed-forward compressor, RMS detection, fixed 3 ms attack
    double lv[2] = {0, 0};
    for (int k = 0; k < nch; ++k) lv[k] = d.det[static_cast<size_t>(k)].process(x[k]);
    if (link) lv[0] = lv[1] = std::max(lv[0], lv[1]);
    for (int k = 0; k < nch; ++k) {
        const double target = gc_.gainDb(20.0 * std::log10(std::max(lv[k], 1e-9)));
        double& gr = d.gr[static_cast<size_t>(k)];
        gr = target + (target < gr ? atk_ : rel_) * (gr - target);
        if (gr != 0.0) x[k] *= std::pow(10.0, gr / 20.0);
    }
}

void Processor::runChain(Chain& c, bool dynFirst, double* x, int nch) {
    auto eq = [&]() { for (int k = 0; k < nch; ++k) { Eq& e = c.eq[static_cast<size_t>(k)]; x[k] = e.hf.process(e.hmf.process(e.lmf.process(e.lf.process(x[k])))); } };
    if (dynFirst) { dynSample(c.dyn, x, nch); eq(); } else { eq(); dynSample(c.dyn, x, nch); }
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    for (int start = 0; start < n; start += kControl) {
        const int len = std::min(kControl, n - start);
        bool moving = false;
        for (LinearSmoother* s : {&hpfF_, &lpfF_, &hf_, &hmf_, &lmf_, &lf_}) if (s->isSmoothing()) { s->skip(len); moving = true; }
        if (moving) updateFilters(len);
        for (int i = start; i < start + len; ++i) {
            const double hpOn = hpfOn_.next(), lpOn = lpfOn_.next(), rt = route_.next(), fd = fader_.next();
            double x[2] = {0, 0};
            for (int k = 0; k < nch; ++k) {
                double v = ch[k][i];
                const double hp = hp2_[static_cast<size_t>(k)].process(hp1_[static_cast<size_t>(k)].process(v));
                v = v + hpOn * (hp - v);
                const double lp = lp_[static_cast<size_t>(k)].process(v);
                x[k] = v + lpOn * (lp - v);
            }
            double a[2] = {x[0], x[1]}, b[2] = {x[0], x[1]};
            if (rt < 1.0) runChain(chain_[0], true, a, nch);
            if (rt > 0.0) runChain(chain_[1], false, b, nch);
            for (int k = 0; k < nch; ++k) {
                double y = rt <= 0.0 ? a[k] : rt >= 1.0 ? b[k] : a[k] + rt * (b[k] - a[k]);
                y *= fd;
                ch[k][i] = static_cast<float>(std::abs(y) < 1e-30 ? 0.0 : y);
            }
        }
    }
}

}  // namespace sw::cs02
