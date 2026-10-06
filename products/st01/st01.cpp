#include "st01/st01.hpp"
#include <algorithm>
#include <cmath>

namespace sw::st01 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"st01.low",       "Low width",     0, 200, 100, Curve::Lin, 1, {}, "%"},
        {"st01.lomid",     "Lo mid width",  0, 200, 100, Curve::Lin, 1, {}, "%"},
        {"st01.himid",     "Hi mid width",  0, 200, 100, Curve::Lin, 1, {}, "%"},
        {"st01.high",      "High width",    0, 200, 100, Curve::Lin, 1, {}, "%"},
        {"st01.xover1",    "Crossover 1",   20, 20000, 200,  Curve::Log, 1, {}, "Hz"},
        {"st01.xover2",    "Crossover 2",   20, 20000, 2000, Curve::Log, 1, {}, "Hz"},
        {"st01.xover3",    "Crossover 3",   20, 20000, 8000, Curve::Log, 1, {}, "Hz"},
        {"st01.monocheck", "Mono check",    0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}, nullptr, nullptr, 1.0, false},
    };
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::setSplit() {
    const auto f = Lr4Split4::effective(target_[Xover1], target_[Xover2], target_[Xover3], fs_);
    if (f[0] == splitF_[0] && f[1] == splitF_[1] && f[2] == splitF_[2]) return;
    split_.setup(f[0], f[1], f[2], fs_);
    splitF_[0] = f[0]; splitF_[1] = f[1]; splitF_[2] = f[2];
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    splitF_[0] = splitF_[1] = splitF_[2] = 0.0;
    setSplit();
    sLR_.fill(0.0); sLL_.fill(0.0); sRR_.fill(0.0); energy_.fill(0.0);
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::snapToTargets() {}

double Processor::correlation(int band) const {
    const size_t b = static_cast<size_t>(band);
    const double d = std::sqrt(sLL_[b] * sRR_[b]);
    return d < 1e-12 ? 1.0 : std::clamp(sLR_[b] / d, -1.0, 1.0);
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    if (numCh < 2) return;   // a mono track has no width
    setSplit();
    const double w[4] = {target_[Low] * 0.01, target_[LoMid] * 0.01, target_[HiMid] * 0.01, target_[High] * 0.01};
    const bool mono = target_[MonoCheck] > 0.5;
    const double k = 1.0 - std::exp(-1.0 / (0.3 * fs_));
    for (int i = 0; i < n; ++i) {
        double bl[4], br[4];
        split_.process(0, ch[0][i], bl); split_.process(1, ch[1][i], br);
        double l = 0.0, r = 0.0;
        for (int b = 0; b < 4; ++b) {
            const double m = 0.5 * (bl[b] + br[b]), s = 0.5 * (bl[b] - br[b]) * w[b];
            const double ol = m + s, orr = m - s;
            l += ol; r += orr;
            const size_t q = static_cast<size_t>(b);
            sLR_[q] += k * (ol * orr - sLR_[q]); sLL_[q] += k * (ol * ol - sLL_[q]); sRR_[q] += k * (orr * orr - sRR_[q]); energy_[q] += k * (0.5 * (ol * ol + orr * orr) - energy_[q]);
        }
        if (mono) { const double m = 0.5 * (l + r); l = r = m; }
        if (std::abs(l) < 1e-30) l = 0.0;
        if (std::abs(r) < 1e-30) r = 0.0;
        ch[0][i] = static_cast<float>(l); ch[1][i] = static_cast<float>(r);
    }
}

}  // namespace sw::st01
