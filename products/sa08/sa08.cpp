#include "sa08/sa08.hpp"
#include <algorithm>
#include <cmath>

namespace sw::sa08 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"sa08.bits",       "Bits",        1, 24, 8,       Curve::Lin, 1, {}, "bit"},
        {"sa08.rate",       "Rate",        200, 48000, 11000, Curve::Log, 1, {}, "Hz"},
        {"sa08.jitter",     "Jitter",      0, 100, 2,      Curve::Lin, 1, {}, "%"},
        {"sa08.mix",        "Mix",         0, 100, 70,     Curve::Lin, 1, {}, "%"},
        {"sa08.prefilter",  "Pre filter",  0, 1, 1,        Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"sa08.postfilter", "Post filter", 0, 1, 0,        Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"sa08.dither",     "Dither",      0, 1, 0,        Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"sa08.evo.on",     "Tempo lock",  0, 1, 0,        Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    c_[0].rng = 0x1badf00du; c_[1].rng = 0x2545f491u; jrng_ = 0x9e3779b9u;
    for (auto& c : c_) c.held = 0;
    counter_ = 1.0;   // the first sample is taken
    updateRate();
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    if (id == Bits) { v = std::round(v); lsb_ = std::ldexp(1.0, 1 - static_cast<int>(v)); }
    target_[static_cast<size_t>(id)] = v;
    if (id == Rate || id == TempoLock) updateRate();
}

void Processor::updateRate() {
    double r = target_[Rate];
    if (target_[TempoLock] > 0.5 && bpm_ > 0.0) {
        const double fb = bpm_ / 60.0;
        r = std::max(1.0, std::round(r / fb)) * fb;
    }
    rate_ = r;
    const double fc = std::min(0.45 * rate_, 0.45 * fs_);
    for (auto& c : c_) { c.pre.setup(fc, fs_); c.post.setup(fc, fs_); }
}

double Processor::rnd(int ch) {
    uint32_t& s = c_[static_cast<size_t>(ch)].rng;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s / 2147483648.0 - 1.0;
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const bool pre = target_[PreFilter] > 0.5, post = target_[PostFilter] > 0.5, dith = target_[Dither] > 0.5;
    const bool bypassHold = rate_ >= fs_ * 0.9995;
    const double period = fs_ / rate_, jit = target_[Jitter] * 0.01 * 0.5;   // Jitter 100 % = period +-50 %
    const double inv = 1.0 / lsb_;
    for (int i = 0; i < n; ++i) {
        bool take = bypassHold;
        if (!bypassHold) {
            counter_ -= 1.0;
            if (counter_ <= 1e-6) {   // tolerance: Rate comes back from the curve with ~1e-12 relative error
                take = true;
                double r = 0.0;
                if (jit > 0.0) { jrng_ ^= jrng_ << 13; jrng_ ^= jrng_ >> 17; jrng_ ^= jrng_ << 5; r = jrng_ / 2147483648.0 - 1.0; }
                counter_ += std::max(1.0, period * (1.0 + jit * r));
            }
        }
        for (int c = 0; c < nch; ++c) {
            Ch& s = c_[static_cast<size_t>(c)];
            double x = ch[c][i];
            if (pre) x = s.pre.process(x);
            if (take) {
                double v = x * inv;
                if (dith) v += 0.5 * (rnd(c) + rnd(c));   // TPDF, +-1 LSB
                s.held = std::clamp(std::floor(v + 0.5) * lsb_, -1.0, 1.0);
            }
            double y = s.held;
            if (post) y = s.post.process(y);
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::sa08
