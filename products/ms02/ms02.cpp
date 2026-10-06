#include "ms02/ms02.hpp"
#include <algorithm>
#include <cmath>

namespace sw::ms02 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"ms02.gain",      "Gain",       0, 24, 0,       Curve::Lin,  1, {}, "dB"},
            {"ms02.ceiling",   "Ceiling",    -12, 0, -1,     Curve::Lin,  1, {}, "dBTP"},
            {"ms02.release",   "Release",    1, 1000, 1000,  Curve::Skew, 3, {}, "ms"},
            {"ms02.lookahead", "Lookahead",  0.5, 5, 1.5,    Curve::Lin,  1, {}, "ms"},
            {"ms02.tp",        "True peak",  0, 1, 1,        Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"ms02.isp",       "ISP detect", 4, 8, 8,        Curve::Step, 1, {4, 8}, "", {"4x", "8x"}},
            {"ms02.link",      "Link",       0, 100, 100,    Curve::Lin,  1, {}, "%"},
            {"ms02.dither",    "Dither",     0, 24, 0,       Curve::Step, 1, {0, 16, 24}, "", {"Off", "16 bit", "24 bit"}},
        };
        v[Release].maxLabel = "Auto";  // rightmost position = program dependent release
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

int Processor::latencySamples() const {
    const int la = std::max(1, static_cast<int>(std::lround(target_[LookaheadMs] * 0.001 * fs_)));
    return la + (target_[TruePeak] > 0.5 ? TruePeakDetector::kTapsPerPhase : 0);  // detector delay + flat-gain margin
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    const int la = std::max(1, static_cast<int>(std::lround(target_[LookaheadMs] * 0.001 * fs_)));
    lim_.prepare(fs_, 2, la, target_[TruePeak] > 0.5, static_cast<int>(target_[Isp]));
    gain_.reset(fs_, 20.0, std::pow(10.0, target_[Gain] / 20.0));
    sustained_ = 0;
    applyLimiterSettings();
}

void Processor::applyLimiterSettings() {
    const bool autoRel = target_[Release] >= specs()[Release].max;
    lim_.set(target_[Ceiling] - 0.02, autoRel ? 30.0 : target_[Release], target_[Link] / 100.0);  // 0.02 dB guard band
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Gain) gain_.setTarget(std::pow(10.0, v / 20.0));
    else if (id == Ceiling || id == Release || id == Link) applyLimiterSettings();
    // LookaheadMs / TruePeak / Isp change the latency: they apply at the next prepare (host restart)
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    for (int i = 0; i < n; ++i) {
        const double g = gain_.next();
        for (int c = 0; c < nch; ++c) ch[c][i] = static_cast<float>(ch[c][i] * g);
    }
    lim_.process(ch, nch, n);
    // Auto release: short reductions recover fast (30 ms), sustained ones slowly (300 ms)
    if (target_[Release] >= specs()[Release].max) {
        sustained_ = lim_.gainReductionDb() < -1.0 ? sustained_ + n : 0;
        lim_.setReleaseMs(sustained_ > static_cast<int>(0.1 * fs_) ? 300.0 : 30.0);
    }
    const int bits = static_cast<int>(target_[Dither]);
    if (bits > 0) {  // TPDF dither + requantization
        const double q = std::ldexp(1.0, bits - 1);
        auto uni = [this] { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_ / 4294967296.0 - 0.5; };
        for (int c = 0; c < nch; ++c)
            for (int i = 0; i < n; ++i) ch[c][i] = static_cast<float>(std::round(ch[c][i] * q + uni() + uni()) / q);
    }
}

}  // namespace sw::ms02
