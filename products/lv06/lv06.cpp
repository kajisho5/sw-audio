#include "lv06/lv06.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv06 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv06.target",   "Target",    0, 3, 0,    Curve::Step, 1, {0, 1, 2, 3}, "", {"Stream -14", "Podcast -16", "Broadcast -24", "Custom"}},
        {"lv06.custom",   "Custom",    -30, -5, -14, Curve::Lin, 1, {}, "LUFS"},
        {"lv06.ride",     "Ride speed", 0, 2, 0,   Curve::Step, 1, {0, 1, 2}, "", {"Slow", "Medium", "Fast"}},
        {"lv06.boost",    "Max boost", 0, 12, 6,   Curve::Lin, 1, {}, "dB"},
        {"lv06.ceiling",  "Ceiling",   -3, 0, -1,  Curve::Lin, 1, {}, "dBFS"},
        {"lv06.monosafe", "Mono safe", 0, 1, 0,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

double targetLufs(int t, double custom) { static const double k[3] = {-14.0, -16.0, -24.0}; return t >= 0 && t < 3 ? k[t] : custom; }

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate; meter_.setup(fs_, 2); gDb_ = 0; lim_ = 1; seen_ = 0; quiet_ = 0; dead_ = false; corr_ = 0; pLR_ = pLL_ = pRR_ = 0; side_ = 1;
    for (auto& x : x_) { x.lpA.setup(Svf::Mode::LowPass, 150.0, fs_, 0.70710678, 0); x.lpB.setup(Svf::Mode::LowPass, 150.0, fs_, 0.70710678, 0); x.hpA.setup(Svf::Mode::HighPass, 150.0, fs_, 0.70710678, 0); x.hpB.setup(Svf::Mode::HighPass, 150.0, fs_, 0.70710678, 0); }
    prepared_ = true;
}
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    const bool stereo = numCh > 1;
    { const float* in[2] = {ch[0], stereo ? ch[1] : ch[0]}; meter_.process(in, 2, n); }
    seen_ += n;
    const double st = meter_.shortTerm();
    if (st < -60.0) quiet_ += n; else quiet_ = 0;
    dead_ = quiet_ > 2.0 * fs_;
    const double rates[3] = {0.5, 1.5, 4.0}, rate = rates[std::clamp(static_cast<int>(target_[Ride]), 0, 2)];
    if (seen_ > fs_ && st > -45.0 && meter_.momentary() > -50.0) {
        const double want = std::clamp(targetLufs(static_cast<int>(target_[Target]), target_[Custom]) - st, -12.0, target_[MaxBoost]);
        const double step = rate * n / fs_;
        gDb_ += std::clamp(want - gDb_, -step, step);
    } else if (gDb_ > target_[MaxBoost]) gDb_ = target_[MaxBoost];
    const double g = std::pow(10.0, gDb_ / 20.0), ceil = std::pow(10.0, target_[Ceiling] / 20.0), relC = std::exp(-1.0 / (0.05 * fs_));
    const bool mono = target_[MonoSafe] > 0.5 && stereo; const double cC = std::exp(-1.0 / (0.3 * fs_));
    for (int i = 0; i < n; ++i) {
        double y[2] = {ch[0][i] * g, stereo ? ch[1][i] * g : 0.0};
        if (mono) {
            double lo[2], hi[2];
            for (int c = 0; c < 2; ++c) { X& x = x_[static_cast<size_t>(c)]; lo[c] = x.lpB.process(x.lpA.process(y[c])); hi[c] = x.hpB.process(x.hpA.process(y[c])); }
            const double m = 0.5 * (lo[0] + lo[1]); y[0] = hi[0] + m; y[1] = hi[1] + m;
            pLR_ = cC * pLR_ + (1 - cC) * y[0] * y[1]; pLL_ = cC * pLL_ + (1 - cC) * y[0] * y[0]; pRR_ = cC * pRR_ + (1 - cC) * y[1] * y[1];
            const double d = std::sqrt(pLL_ * pRR_); corr_ = d > 1e-12 ? pLR_ / d : 0.0;
            const double tgt = corr_ < 0.0 ? std::pow(10.0, -12.0 * std::min(1.0, -corr_) / 20.0) : 1.0;
            side_ += (tgt - side_) * (1.0 - cC) * 4.0;
            const double mid = 0.5 * (y[0] + y[1]), sd = 0.5 * (y[0] - y[1]) * side_; y[0] = mid + sd; y[1] = mid - sd;
        }
        double pk = std::abs(y[0]); if (stereo) pk = std::max(pk, std::abs(y[1]));
        const double need = pk > ceil ? ceil / pk : 1.0;
        lim_ = need < lim_ ? need : relC * lim_ + (1 - relC) * need;
        if (lim_ * pk > ceil) lim_ = ceil / pk;
        for (int c = 0; c < (stereo ? 2 : 1); ++c) { const float o = static_cast<float>(y[c] * lim_); ch[c][i] = std::abs(o) < 1e-30f ? 0.0f : o; }
    }
}

}  // namespace sw::lv06
