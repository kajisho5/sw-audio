#include "cr01/cr01.hpp"
#include <algorithm>
#include <cmath>

namespace sw::cr01 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"cr01.type",   "Type",       0, 3, 0,       Curve::Step, 1, {0, 1, 2, 3}, "", {"LP", "BP", "HP", "Notch"}},
        {"cr01.mod",    "Mod source", 0, 2, 0,       Curve::Step, 1, {0, 1, 2}, "", {"Envelope", "LFO", "Sidechain"}},
        {"cr01.cutoff", "Cutoff",     20, 20000, 1200, Curve::Log, 1, {}, "Hz"},
        {"cr01.res",    "Resonance",  0, 100, 60,    Curve::Lin, 1, {}, "%"},
        {"cr01.env",    "Env amount", -100, 100, 40, Curve::Lin, 1, {}, "%"},
        {"cr01.drive",  "Drive",      0, 100, 20,    Curve::Lin, 1, {}, "%"},
        {"cr01.evo.on", "Adaptive range", 0, 1, 1,   Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        oversampleSpec("cr01.os"),
    };
    return s;
}

namespace {
constexpr double kPi = 3.14159265358979323846, kOctaves = 5.0, kMinRangeDb = 6.0;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; cutoffNow_ = target_[Cutoff]; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& c : ch_) { c.s1 = c.s2 = 0.0; c.os.reset(); }
    env_ = 0.0; envDb_ = -120.0; hi_ = -60.0; lo_ = -90.0; m_ = 0.0; ph_ = 0.0; blkMax_ = -200.0; blkMin_ = 1e9; blkCount_ = 0; ringPos_ = 0; ringMax_.fill(-200.0); ringMin_.fill(1e9); cutoffNow_ = target_[Cutoff]; prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Oversample) for (auto& c : ch_) c.os.setFactor(static_cast<int>(v));   // the filter's coefficients follow the rate (process)
}

void Processor::updateMod(double level) {
    const int src = static_cast<int>(target_[ModSource] + 0.5);
    if (src == Lfo) { m_ = 0.5 + 0.5 * std::sin(2.0 * kPi * ph_); return; }
    const double db = level > 1e-6 ? 20.0 * std::log10(level) : -120.0;
    envDb_ = db;
    if (target_[Evo] > 0.5) {
        // the range of the last 10 s: the maximum and the minimum of 20 blocks of 0.5 s (silence below -70 dB does not count as a minimum)
        blkMax_ = std::max(blkMax_, db); if (db > -70.0) blkMin_ = std::min(blkMin_, db);
        if (++blkCount_ >= static_cast<int>(0.5 * fs_)) { blkCount_ = 0; ringMax_[static_cast<size_t>(ringPos_)] = blkMax_; ringMin_[static_cast<size_t>(ringPos_)] = blkMin_; ringPos_ = (ringPos_ + 1) % kBlocks; blkMax_ = -200.0; blkMin_ = 1e9; }
        double hi = blkMax_, lo = blkMin_;
        for (int i = 0; i < kBlocks; ++i) { hi = std::max(hi, ringMax_[static_cast<size_t>(i)]); lo = std::min(lo, ringMin_[static_cast<size_t>(i)]); }
        if (lo > hi) lo = hi - kMinRangeDb;
        if (hi - lo < kMinRangeDb) lo = hi - kMinRangeDb;
        hi_ = hi; lo_ = lo;
        m_ = std::clamp((db - lo) / (hi - lo), 0.0, 1.0);
    } else {
        m_ = std::clamp((db + 40.0) / 40.0, 0.0, 1.0);
    }
}

void Processor::processWithSidechain(float** ch, int numCh, int n, const float* const* sc, int scCh) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const int type = static_cast<int>(target_[Type] + 0.5), src = static_cast<int>(target_[ModSource] + 0.5);
    const double res = target_[Resonance] * 0.01, k = std::max(0.1, 2.0 * (1.0 - 0.95 * res)), amt = target_[EnvAmount] * 0.01, drv = target_[Drive] * 0.01, G = 1.0 + 9.0 * drv;
    const double attack = 1.0 - std::exp(-1.0 / (0.003 * fs_)), release = 1.0 - std::exp(-1.0 / (0.12 * fs_));
    const double lfoHz = bpm_ > 0.0 ? bpm_ / 240.0 : 0.5, smooth = 1.0 - std::exp(-1.0 / (0.005 * fs_));
    const double fsOs = ch_[0].os.rate(fs_);   // the filter runs at the oversampled rate (the common setting, default 2x)
    for (int i = 0; i < n; ++i) {
        double det = 0.0;
        if (src == Sidechain && sc != nullptr && scCh > 0) { for (int c = 0; c < std::min(scCh, 2); ++c) det = std::max(det, static_cast<double>(std::abs(sc[c][i]))); }
        else for (int c = 0; c < nch; ++c) det = std::max(det, static_cast<double>(std::abs(ch[c][i])));
        env_ += (det > env_ ? attack : release) * (det - env_);
        ph_ += lfoHz / fs_; if (ph_ >= 1.0) ph_ -= 1.0;
        updateMod(env_);
        const double wanted = std::clamp(target_[Cutoff] * std::exp2(kOctaves * amt * m_), 10.0, 0.45 * fs_);
        cutoffNow_ += smooth * (wanted - cutoffNow_);
        const double g = std::tan(kPi * std::min(cutoffNow_, 0.45 * fsOs) / fsOs), a1 = 1.0 / (1.0 + g * (g + k));
        for (int c = 0; c < nch; ++c) {
            auto& f = ch_[static_cast<size_t>(c)];
            double y = f.os.process(ch[c][i], [&](double u) {
                const double xin = u + drv * (std::tanh(G * u) / G - u);   // Drive 0: exactly linear
                const double hp = (xin - (k + g) * f.s1 - f.s2) * a1;
                const double bp = g * hp + f.s1;
                const double bpSat = bp + drv * (std::tanh(bp) - bp);
                f.s1 = g * hp + bpSat;
                const double lp = g * bpSat + f.s2;
                f.s2 = g * bpSat + lp;
                return type == LP ? lp : (type == BP ? k * bp : (type == HP ? hp : lp + hp));
            });
            if (!std::isfinite(y)) { y = 0.0; f.s1 = f.s2 = 0.0; }
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::cr01
