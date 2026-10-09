#include "ms04/ms04.hpp"
#include <algorithm>
#include <cmath>

namespace sw::ms04 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
        {"ms04.drive",      "Drive",      0, 24, 0,       Curve::Lin,  1, {}, "dB"},
        {"ms04.ceiling",    "Ceiling",    -12, 0, -0.3,   Curve::Lin,  1, {}, "dB"},
        {"ms04.knee",       "Knee",       0, 100, 50,     Curve::Lin,  1, {}, "%"},
        {"ms04.mix",        "Mix",        0, 100, 100,    Curve::Lin,  1, {}, "%"},
        {"ms04.os",         "Oversample", 4, 16, 8,       Curve::Step, 1, {4, 8, 16}, "", {"4x", "8x", "16x"}},
        {"ms04.gainmatch",  "Gain match", 0, 1, 1,        Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"ms04.listen",     "Listen",     0, 1, 0,        Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"ms04.lowlat",     "Low lat",    0, 1, 0,        Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        };
        v[Listen].automatable = false;  // monitoring: hear only what the clipper removed (spec: Auto —)
        return v;
    }();
    return s;
}

double clipCurve(double u, double k) {
    auto soft = [](double v, double h) {  // clamp with a quadratic knee of half-width h around 1
        const double a = std::abs(v), s = v < 0 ? -1.0 : 1.0;
        if (h <= 0.0) return std::clamp(v, -1.0, 1.0);
        if (a <= 1.0 - h) return v;
        if (a >= 1.0 + h) return s;
        const double d = a - (1.0 - h);
        return s * (a - d * d / (4.0 * h));
    };
    k = std::clamp(k, 0.0, 1.0);
    if (k <= 0.5) return soft(u, k);
    const double t = (k - 0.5) * 2.0, sv = soft(u, 0.5);
    return sv + t * (std::tanh(u) - sv);
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int maxBlock) {
    fs_ = sampleRate;
    const int f[3] = {4, 8, 16};
    iir_ = target_[LowLat] > 0.5;
    for (auto& ch : os_) for (int i = 0; i < 3; ++i) ch[static_cast<size_t>(i)].setup(f[i]);
    for (auto& ch : osIir_) for (int i = 0; i < 3; ++i) ch[static_cast<size_t>(i)].setup(f[i]);
    for (auto& ch : osDry_) for (int i = 0; i < 3; ++i) ch[static_cast<size_t>(i)].setup(f[i]);
    active_ = previous_ = factorIndex(target_[Oversample]);
    fade_ = 0;
    fadeLen_ = std::max(1, static_cast<int>(std::lround(fs_ * 0.01)));
    buf_.assign(16, 0.0);
    for (auto& d : dly_) d.assign(static_cast<size_t>(iir_ ? 1 : FirOversampler::kTapsPerPhase), 0.0);
    dpos_ = 0;
    drive_.reset(fs_, 20.0, std::pow(10.0, target_[Drive] / 20.0));
    ceil_.reset(fs_, 20.0, std::pow(10.0, target_[Ceiling] / 20.0));
    knee_.reset(fs_, 20.0, target_[Knee] / 100.0);
    (void)maxBlock;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case Drive: drive_.setTarget(std::pow(10.0, v / 20.0)); break;
        case Ceiling: ceil_.setTarget(std::pow(10.0, v / 20.0)); break;
        case Knee: knee_.setTarget(v / 100.0); break;
        case Oversample: {
            const int idx = factorIndex(v);
            if (idx != active_) {  // 10 ms crossfade; both oversamplers run meanwhile (same latency)
                previous_ = active_; active_ = idx; fade_ = fadeLen_;
                for (auto& ch : os_) ch[static_cast<size_t>(idx)].reset();
                for (auto& ch : osIir_) ch[static_cast<size_t>(idx)].reset();
                for (auto& ch : osDry_) ch[static_cast<size_t>(idx)].reset();
            }
            break;
        }
        default: break;  // Mix: sw::Shell, GainMatch: read per block
    }
}

void Processor::snapToTargets() { drive_.skip(1 << 30); ceil_.skip(1 << 30); knee_.skip(1 << 30); }

template <class Os> double Processor::runOne(Os& os, double x, double c, double k) {
    os.up(x, buf_.data());
    for (int j = 0; j < os.factor(); ++j) buf_[static_cast<size_t>(j)] = c * clipCurve(buf_[static_cast<size_t>(j)] / c, k);
    return os.down(buf_.data());
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const bool match = target_[GainMatch] > 0.5, listen = target_[Listen] > 0.5;
    const int L = iir_ ? 1 : FirOversampler::kTapsPerPhase;   // the dry path's delay: the oversampler's (the IIR one reports none)
    for (int i = 0; i < n; ++i) {
        const double dg = drive_.next(), c = ceil_.next(), k = knee_.next();
        const double w = fade_ > 0 ? static_cast<double>(fade_--) / fadeLen_ : 0.0;  // weight of the previous factor
        for (int cidx = 0; cidx < nch; ++cidx) {
            const double x = ch[cidx][i] * dg;
            double y;
            if (iir_) { auto& o = osIir_[static_cast<size_t>(cidx)]; y = runOne(o[static_cast<size_t>(active_)], x, c, k); if (w > 0.0) y += w * (runOne(o[static_cast<size_t>(previous_)], x, c, k) - y); }
            else { auto& o = os_[static_cast<size_t>(cidx)]; y = runOne(o[static_cast<size_t>(active_)], x, c, k); if (w > 0.0) y += w * (runOne(o[static_cast<size_t>(previous_)], x, c, k) - y); }
            auto& d = dly_[static_cast<size_t>(cidx)];  // the input aligned with the oversampler latency
            double xd = iir_ ? x : d[static_cast<size_t>(dpos_)];
            if (iir_ && listen) { auto& o = osDry_[static_cast<size_t>(cidx)][static_cast<size_t>(active_)]; o.up(x, buf_.data()); xd = o.down(buf_.data()); }   // through the same half-bands, without the clipper
            if (!iir_) d[static_cast<size_t>(dpos_)] = x;
            if (listen) y = xd - y;                  // Listen: only the part the clipper removed
            if (match) y /= dg;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[cidx][i] = static_cast<float>(y);
        }
        dpos_ = (dpos_ + 1) % L;
    }
}

}  // namespace sw::ms04
