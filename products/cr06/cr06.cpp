#include "cr06/cr06.hpp"
#include "sw/tail.hpp"
#include <algorithm>
#include <cmath>

namespace sw::cr06 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"cr06.effect", "Effect", 0, 5, 2,   Curve::Step, 1, {0, 1, 2, 3, 4, 5}, "", {"Wide", "Warm", "Air", "Punch", "Space", "Lo-fi"}},
        {"cr06.amount", "Amount", 0, 10, 0,  Curve::Lin, 1, {}, ""},
        {"cr06.mix",    "Mix",    0, 100, 100, Curve::Lin, 1, {}, "%"},
        {"cr06.out",    "Output", -10, 10, 0, Curve::Lin, 1, {}, "dB"},
        {"cr06.macro",  "Macro",  0, 1, 0,   Curve::Step, 1, {0, 1}, "", {"Off", "On"}, nullptr, nullptr, 1.0, false},
            unitSpec("cr06.unit"),
    };
    return s;
}

namespace {
double lerp(double a, double b, double t) { return a + (b - a) * t; }
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int maxBlock) {
    fs_ = sampleRate; maxBlock_ = std::max(1, maxBlock);
    plate_.setParam(rv02::Decay, 1.2); plate_.setParam(rv02::PreDelay, 15); plate_.setParam(rv02::Damping, 60); plate_.setParam(rv02::LowCut, 150); plate_.setParam(rv02::Width, 100);
    plate_.setParam(rv02::Mix, 100); plate_.setParam(rv02::MonoIn, 0); plate_.setParam(rv02::Sync, 0); plate_.setParam(rv02::DuckOn, 0);
    plate_.prepare(fs_, maxBlock_); plate_.snapToTargets();
    for (auto& s : send_) s.assign(static_cast<size_t>(maxBlock_), 0.0f);
    for (auto& c : ch_) { c.a.reset(); c.b.reset(); c.c.reset(); }
    fastE_ = slowE_ = 0.0; hold_[0] = hold_[1] = 0.0; holdCount_ = 0; lastEffect_ = -1; lastAmount_ = -1.0;
    prepared_ = true;
    setFilters();
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (prepared_ && (id == Effect || id == Amount)) setFilters();
}

double Processor::macroValue(int i) const {
    const double t = target_[Amount] * 0.1;
    switch (static_cast<int>(target_[Effect] + 0.5)) {
        case Wide:  return i == 0 ? 1.0 + 2.0 * t : (i == 1 ? 3.0 * t : 0.0);
        case Warm:  return i == 0 ? 1.0 + 3.0 * t : (i == 1 ? 3.0 * t : -3.0 * t);
        case Air:   return i == 0 ? 6.0 * t : (i == 1 ? 0.1 * t : 0.0);
        case Punch: return i == 0 ? 1.5 * t : (i == 1 ? -2.0 * t : 0.0);
        case Space: return i == 0 ? 0.5 * t : 0.0;
        default:    return i == 0 ? 1.0 + 11.0 * t : (i == 1 ? 6.0 + 10.0 * (1.0 - t) : lerp(12000.0, 3000.0, t));
    }
}

void Processor::setFilters() {
    const double t = target_[Amount] * 0.1;
    for (auto& c : ch_) {
        switch (static_cast<int>(target_[Effect] + 0.5)) {
            case Wide:  c.a.setup(Svf::Mode::HighShelf, 4000.0, fs_, 0.7071, 3.0 * t); break;
            case Warm:  c.a.setup(Svf::Mode::LowShelf, 150.0, fs_, 0.7071, 3.0 * t); c.b.setup(Svf::Mode::HighShelf, 6000.0, fs_, 0.7071, -3.0 * t); break;
            case Air:   c.a.setup(Svf::Mode::HighShelf, 10000.0, fs_, 0.7071, 6.0 * t); c.b.setup(Svf::Mode::HighPass, 6000.0, fs_, 0.7071, 0.0); break;
            case Lofi:  c.a.setup(Svf::Mode::LowPass, lerp(12000.0, 3000.0, t), fs_, 0.7071, 0.0); break;
            default: break;
        }
    }
    lastEffect_ = static_cast<int>(target_[Effect] + 0.5); lastAmount_ = t;
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const double t = target_[Amount] * 0.1;
    if (t <= 0.0) return;   // Amount 0: the dry signal, bit for bit
    const int fx = static_cast<int>(target_[Effect] + 0.5);
    if (fx == Space) {
        for (int off = 0; off < n; off += maxBlock_) {
            const int len = std::min(maxBlock_, n - off); float* sp[2] = {send_[0].data(), send_[nch > 1 ? 1 : 0].data()};
            for (int c = 0; c < nch; ++c) for (int i = 0; i < len; ++i) sp[c][i] = ch[c][off + i];
            plate_.process(sp, nch, len);
            for (int c = 0; c < nch; ++c) for (int i = 0; i < len; ++i) { double y = ch[c][off + i] + 0.5 * t * sp[c][i]; if (!std::isfinite(y) || std::abs(y) < 1e-30) y = 0.0; ch[c][off + i] = static_cast<float>(y); }
        }
        return;
    }
    const double fa = 1.0 - std::exp(-1.0 / (0.002 * fs_)), fs = 1.0 - std::exp(-1.0 / (0.04 * fs_));
    for (int i = 0; i < n; ++i) {
        double x[2] = {ch[0][i], nch > 1 ? ch[1][i] : ch[0][i]}, y[2] = {x[0], x[1]};
        switch (fx) {
            case Wide: if (nch > 1) {
                const double m = 0.5 * (x[0] + x[1]), sd = ch_[0].a.process(0.5 * (x[0] - x[1])) * (1.0 + 2.0 * t);
                y[0] = m + sd; y[1] = m - sd;
            } break;
            case Warm: for (int c = 0; c < nch; ++c) { const double d = 1.0 + 3.0 * t, sat = std::tanh(d * x[c]) / std::tanh(d); double v = x[c] + t * (sat - x[c]); v = ch_[static_cast<size_t>(c)].a.process(v); y[c] = ch_[static_cast<size_t>(c)].b.process(v); } break;
            case Air: for (int c = 0; c < nch; ++c) { const double hp = ch_[static_cast<size_t>(c)].b.process(x[c]); y[c] = ch_[static_cast<size_t>(c)].a.process(x[c]) + 0.1 * t * std::tanh(3.0 * hp); } break;
            case Punch: {
                const double lvl = std::max(std::abs(x[0]), std::abs(x[1]));
                fastE_ += fa * (lvl - fastE_); slowE_ += fs * (lvl - slowE_);
                const double rise = std::clamp((fastE_ - slowE_) / (slowE_ + 1e-4), 0.0, 4.0) / 4.0;
                const double g = std::pow(10.0, (rise * 8.0 * t - (1.0 - rise) * 2.0 * t * (slowE_ > 1e-3 ? 1.0 : 0.0)) / 20.0);
                y[0] = x[0] * g; y[1] = x[1] * g;
            } break;
            default: {   // Lo-fi
                if (holdCount_ <= 0) { const double bits = 6.0 + 10.0 * (1.0 - t), q = std::pow(2.0, bits - 1.0); hold_[0] = std::round(x[0] * q) / q; hold_[1] = std::round(x[1] * q) / q; holdCount_ = static_cast<int>(std::lround(1.0 + 11.0 * t)); }
                --holdCount_;
                for (int c = 0; c < nch; ++c) y[c] = ch_[static_cast<size_t>(c)].a.process(hold_[c]);
            } break;
        }
        for (int c = 0; c < nch; ++c) { double v = y[c]; if (!std::isfinite(v) || std::abs(v) < 1e-30) v = 0.0; ch[c][i] = static_cast<float>(v); }
    }
}

// the tail: only Space (the plate reverb core) rings on; the others are filters, followers and a sample-rate reducer
double Processor::tailSeconds() const { return static_cast<int>(target_[Effect] + 0.5) == Space && target_[Amount] > 0.0 ? plate_.tailSeconds() : 0.0; }

}  // namespace sw::cr06
