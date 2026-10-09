#include "cs01/cs01.hpp"
#include <algorithm>
#include <cmath>

namespace sw::cs01 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"cs01.pre.drive",    "Drive",   0, 10, 2,        Curve::Lin,  1, {}, ""},
        {"cs01.hpf",          "HPF",     0, 160, 0,       Curve::Step, 1, {0, 50, 80, 160}, "Hz", {"Off", "50 Hz", "80 Hz", "160 Hz"}},
        {"cs01.eq.high",      "High",    -16, 16, 0,      Curve::Lin,  1, {}, "dB"},
        {"cs01.eq.midfreq",   "Mid kHz", 700, 4800, 1600, Curve::Step, 1, {700, 1600, 3200, 4800}, "Hz"},
        {"cs01.eq.mid",       "Mid",     -18, 18, 0,      Curve::Lin,  1, {}, "dB"},
        {"cs01.eq.low",       "Low",     -16, 16, 0,      Curve::Lin,  1, {}, "dB"},
        {"cs01.comp.thresh",  "Thresh",  0, 10, 0,        Curve::Lin,  1, {}, ""},
        {"cs01.comp.ratio",   "Ratio",   2, 20, 4,        Curve::Step, 1, {2, 4, 8, 20}, ":1", {"2:1", "4:1", "8:1", "20:1"}},
        {"cs01.comp.release", "Release", 50, 1500, 200,   Curve::Skew, 2, {}, "ms"},
        {"cs01.order",        "Order",   0, 1, 0,         Curve::Step, 1, {0, 1}, "", {"EQ first", "Comp first"}},
        {"cs01.comp.mix",     "Mix",     0, 100, 100,     Curve::Lin,  1, {}, "%"},
        {"cs01.out",          "Output",  -10, 10, 0,      Curve::Lin,  1, {}, "dB"},
        {"cs01.link",         "Link",    0, 1, 1,         Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        oversampleSpec("cs01.os"),
    };
    return s;
}

namespace {
constexpr int kControl = 16;
constexpr double kIronSplitHz = 250.0, kIronHighShare = 0.35;  // design: lows take the full drive, highs about a third
constexpr double kHeadroom = 2.0;  // design: the stage saturates toward +6 dBFS, so Drive 0 stays clean at normal levels
double ironStage(double x, double g) { return kHeadroom * std::tanh(g * x / kHeadroom) / g; }
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (LinearSmoother* s : {&hpfF_, &high_, &midF_, &mid_, &low_, &drive_, &mix_}) s->reset(fs_, 20.0, 0.0);
    hpfOn_.reset(fs_, 10.0, 0.0);
    order_.reset(fs_, 10.0, 0.0);
    chain_ = {};
    os_ = {};
    for (auto& f : split_) f.reset();
    updateSplit();
    for (auto& c : chain_) for (auto& d : c.comp.det) d.set(fs_, LevelDetector::Mode::Rms);
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
    snapToTargets();
}

// the iron's low split is a filter inside the oversampled loop: its coefficient is for the oversampled rate (the common setting, default 2x)
void Processor::updateSplit() { for (auto& f : split_) f.setup(OnePole::Mode::LowPass, kIronSplitHz, os_[0].rate(fs_)); }

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case Drive: drive_.setTarget(std::pow(10.0, v * 1.8 / 20.0)); break;
        case Hpf: hpfOn_.setTarget(v > 0 ? 1.0 : 0.0); if (v > 0) hpfF_.setTarget(std::log(v)); break;
        case High: high_.setTarget(v); break;
        case MidFreq: midF_.setTarget(std::log(v)); break;
        case Mid: mid_.setTarget(v); break;
        case Low: low_.setTarget(v); break;
        case Thresh: case Ratio: gc_.set(-4.0 * target_[Thresh], target_[Ratio], 0.0); break;  // 0..10 -> 0..-40 dBFS
        case Release: relCoef_ = Ballistics::coef(fs_, v); break;
        case Order: order_.setTarget(v); break;
        case Mix: mix_.setTarget(v / 100.0); break;
        case Oversample: for (auto& o : os_) o.setFactor(static_cast<int>(v)); updateSplit(); break;
        default: break;  // Output: sw::Shell, Link: per sample
    }
}

void Processor::snapToTargets() {
    for (LinearSmoother* s : {&hpfF_, &hpfOn_, &high_, &midF_, &mid_, &low_, &drive_, &mix_, &order_}) s->skip(1 << 30);
    if (hpfF_.current() == 0.0) hpfF_.reset(fs_, 20.0, std::log(50.0));
    updateEq(0);
}

void Processor::updateEq(int ramp) {  // EQ04 circuit: HPF 18 dB/oct, 60 Hz shelf (Q 1.0 bump), mid bell Q 0.9, 12 kHz shelf
    const double hf = std::exp(hpfF_.current() > 0 ? hpfF_.current() : std::log(50.0));
    for (auto& c : chain_)
        for (auto& e : c.eq) {
            e.hp1.setupRamp(OnePole::Mode::HighPass, hf, fs_, ramp);
            e.hp2.setupRamp(Svf::Mode::HighPass, hf, fs_, 1.0, 0, ramp);
            e.low.setupRamp(Svf::Mode::LowShelf, 60.0, fs_, 1.0, low_.current(), ramp);
            e.mid.setupRamp(Svf::Mode::Bell, std::exp(midF_.current()), fs_, 0.9, mid_.current(), ramp);
            e.high.setupRamp(Svf::Mode::HighShelf, std::min(12000.0, 0.45 * fs_), fs_, 0.70710678, high_.current(), ramp);
        }
}

double Processor::eqSample(Eq& e, double x, double hpOn) {
    const double hp = e.hp2.process(e.hp1.process(x));
    x = x + hpOn * (hp - x);
    return e.high.process(e.mid.process(e.low.process(x)));
}

void Processor::compSample(Comp& c, double* x, int nch) {
    // feedback: the detector listens to the compressor's own output (previous sample)
    const bool link = target_[Link] > 0.5;
    double lv[2] = {0, 0};
    for (int k = 0; k < nch; ++k) lv[k] = c.det[static_cast<size_t>(k)].process(c.lastOut[static_cast<size_t>(k)]);
    if (link && nch == 2) lv[0] = lv[1] = std::max(lv[0], lv[1]);
    const double mix = mix_.current();
    for (int k = 0; k < nch; ++k) {
        const double out = 20.0 * std::log10(std::max(lv[k], 1e-9)) - gc_.t_;
        const double target = out > 0 ? -(target_[Ratio] - 1.0) * out : 0.0;
        double& gr = c.grDb[static_cast<size_t>(k)];
        if (target < gr) gr = target + Ballistics::coef(fs_, std::clamp(20.0 / (1.0 + std::max(0.0, out) / 6.0), 2.0, 20.0)) * (gr - target);
        else gr = target + relCoef_ * (gr - target);
        const double y = x[k] * std::pow(10.0, gr / 20.0);
        c.lastOut[static_cast<size_t>(k)] = y;
        x[k] = x[k] + mix * (y - x[k]);  // Mix: parallel blend of the compressor only
    }
}

void Processor::runChain(Chain& c, bool eqFirst, double* x, int nch, double hpOn) {
    if (eqFirst) { for (int k = 0; k < nch; ++k) x[k] = eqSample(c.eq[static_cast<size_t>(k)], x[k], hpOn); compSample(c.comp, x, nch); }
    else { compSample(c.comp, x, nch); for (int k = 0; k < nch; ++k) x[k] = eqSample(c.eq[static_cast<size_t>(k)], x[k], hpOn); }
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    for (int start = 0; start < n; start += kControl) {
        const int len = std::min(kControl, n - start);
        bool moving = false;
        for (LinearSmoother* s : {&hpfF_, &high_, &midF_, &mid_, &low_}) if (s->isSmoothing()) { s->skip(len); moving = true; }
        if (moving) updateEq(len);
        for (int i = start; i < start + len; ++i) {
            const double g = drive_.next(), hpOn = hpfOn_.next(), ord = order_.next();
            mix_.next();
            double x[2];
            for (int k = 0; k < nch; ++k) {  // pre: iron at 2x, lows driven harder than highs
                OnePole& split = split_[static_cast<size_t>(k)];
                x[k] = os_[static_cast<size_t>(k)].process(ch[k][i], [&](double u) {
                    const double lo = split.process(u);
                    return ironStage(lo, g) + ironStage(u - lo, g * kIronHighShare + (1.0 - kIronHighShare));
                });
            }
            double a[2] = {x[0], x[1]}, b[2] = {x[0], x[1]};
            if (ord < 1.0) runChain(chain_[0], true, a, nch, hpOn);
            if (ord > 0.0) runChain(chain_[1], false, b, nch, hpOn);
            for (int k = 0; k < nch; ++k) {
                const double y = ord <= 0.0 ? a[k] : ord >= 1.0 ? b[k] : a[k] + ord * (b[k] - a[k]);
                ch[k][i] = static_cast<float>(std::abs(y) < 1e-30 ? 0.0 : y);
            }
        }
    }
}

}  // namespace sw::cs01
