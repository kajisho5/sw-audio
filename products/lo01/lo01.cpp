#include "lo01/lo01.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lo01 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lo01.frequency", "Frequency", 40, 200, 80,   Curve::Log, 1, {}, "Hz"},
        {"lo01.harmonics", "Harmonics", 0, 100, 30,    Curve::Lin, 1, {}, "%"},
        {"lo01.original",  "Original",  -24, 0, 0,     Curve::Lin, 1, {}, "dB"},
        {"lo01.width",     "Width",     0, 2, 0,       Curve::Step, 1, {0, 1, 2}, "", {"Narrow", "Medium", "Wide"}},
        {"lo01.preview",   "Preview",   0, 2, 0,       Curve::Step, 1, {0, 1, 2}, "", {"Off", "Phone safe", "Club"}, nullptr, nullptr, 1.0, false},
    };
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

namespace {
constexpr double kW[4] = {1.0, 0.7, 0.5, 0.35};   // weights of orders 2..5 (design values)
constexpr double kReleaseS = 0.3;                 // level follower release
}

void Processor::updateWeights() {
    const int orders = 2 + static_cast<int>(target_[Width] + 0.5);   // Narrow 2..3, Medium 2..4, Wide 2..5
    double e = 0;
    for (int k = 0; k < orders; ++k) e += kW[k] * kW[k];
    const double norm = 1.0 / std::sqrt(e);
    for (int k = 0; k < 4; ++k) w_[static_cast<size_t>(k)] = k < orders ? kW[k] * norm : 0.0;
}

void Processor::updateFilters(bool ramp) {
    const double f = target_[Frequency];
    const int n = ramp ? static_cast<int>(0.01 * fs_) : 0;
    for (auto& c : c_) {
        if (ramp) { c.lp.ramp(Svf::Mode::LowPass, f, fs_, n); c.hp.ramp(Svf::Mode::HighPass, f, fs_, n); }
        else { c.lp.setup(Svf::Mode::LowPass, f, fs_); c.hp.setup(Svf::Mode::HighPass, f, fs_); }
        c.dc.setup(Svf::Mode::HighPass, 0.5 * f, fs_, 0.70710678, 0);
        c.dc2.setup(Svf::Mode::HighPass, 0.5 * f, fs_, 0.70710678, 0);
    }
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    relDecay_ = std::exp(-1.0 / (kReleaseS * fs_));
    for (auto& c : c_) {
        c.phoneHp.setup(Svf::Mode::HighPass, 300.0, fs_);
        c.bell.setup(Svf::Mode::Bell, 1200.0, fs_, 2.5, 3.0);
        c.clubHp.setup(Svf::Mode::HighPass, 28.0, fs_, 0.70710678, 0);
        c.env = 1e-5;
        c.lp.a.reset(); c.lp.b.reset(); c.hp.a.reset(); c.hp.b.reset(); c.dc.reset(); c.dc2.reset(); c.phoneHp.a.reset(); c.phoneHp.b.reset(); c.bell.reset(); c.clubHp.reset();
    }
    updateWeights();
    updateFilters(false);
    harm_.reset(fs_, 20.0, target_[Harmonics] * 0.01);
    orig_.reset(fs_, 20.0, std::pow(10.0, target_[Original] / 20.0));
    pv_.reset(fs_, 10.0, target_[Preview] > 0.5 ? 1.0 : 0.0);
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case Frequency: if (prepared_) updateFilters(true); break;
        case Harmonics: harm_.setTarget(v * 0.01); break;
        case Original: orig_.setTarget(std::pow(10.0, v / 20.0)); break;
        case Width: updateWeights(); break;
        case Preview: pv_.setTarget(v > 0.5 ? 1.0 : 0.0); break;
        default: break;
    }
}

void Processor::snapToTargets() {
    if (!prepared_) return;
    updateFilters(false);
    for (LinearSmoother* s : {&harm_, &orig_, &pv_}) s->skip(1 << 30);
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const int pm = static_cast<int>(target_[Preview] + 0.5);
    for (int i = 0; i < n; ++i) {
        const double h = harm_.next(), og = orig_.next(), pv = pv_.next();
        for (int c = 0; c < nch; ++c) {
            Ch& s = c_[static_cast<size_t>(c)];
            const double x = ch[c][i];
            const double low = s.lp.process(x), high = s.hp.process(x);
            double harm = 0.0;
            if (h > 0.0 || harm_.isSmoothing()) {
                s.env = std::max({std::abs(low), s.env * relDecay_, 1e-5});
                const double u = std::clamp(low / s.env, -1.0, 1.0), u2 = u * u;
                const double t2 = 2 * u2 - 1, t3 = u * (4 * u2 - 3), t4 = 8 * u2 * (u2 - 1) + 1, t5 = u * (u2 * (16 * u2 - 20) + 5);
                const double gate = std::clamp((s.env - 2e-5) * (1.0 / 8e-5), 0.0, 1.0);   // nothing below about -90 dBFS (T2 and T4 have an offset at 0)
                harm = s.dc2.process(s.dc.process(gate * s.env * (w_[0] * t2 + w_[1] * t3 + w_[2] * t4 + w_[3] * t5))) * h;
            } else {
                s.env = std::max(std::abs(low), s.env * relDecay_);
            }
            double y = high + og * low + harm;
            // Preview: both filters keep running so that switching does not click
            const double ph = s.bell.process(s.phoneHp.process(y)), cl = s.clubHp.process(y);
            if (pv > 0.0) y += pv * ((pm == 1 ? ph : cl) - y);
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::lo01
