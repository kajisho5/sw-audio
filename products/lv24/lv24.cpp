#include "lv24/lv24.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv24 {
namespace {
constexpr double kBaseMs[8] = {29.7, 37.1, 41.1, 53.3, 61.7, 71.9, 83.3, 97.1};   // coprime-ish lengths
constexpr double kScale[3] = {1.0, 0.45, 0.6}, kDamp[3] = {3500.0, 6500.0, 10000.0};
}

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv24.type",  "Type",      0, 2, 0,    Curve::Step, 1, {0, 1, 2}, "", {"Vocal hall", "Room", "Plate"}},
        {"lv24.decay", "Decay",     0.3, 5, 1.8, Curve::Log, 1, {}, "s"},
        {"lv24.pre",   "Pre-delay", 0, 200, 30, Curve::Skew, 2, {}, "ms"},
        {"lv24.tone",  "Tone",      0, 2, 0,    Curve::Step, 1, {0, 1, 2}, "", {"Warm", "Neutral", "Bright"}},
        {"lv24.mix",   "Mix",       0, 100, 18, Curve::Lin, 1, {}, "%"},
        {"lv24.duck",  "Duck",      0, 1, 0,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int maxBlock) {
    fs_ = sampleRate; fdn_.prepare(fs_, 8, 0.4); vd_.prepare(fs_);
    size_t sz = 16; while (sz < static_cast<size_t>(0.21 * fs_) + 16) sz <<= 1; pre_.assign(sz, 0.0f); preMask_ = sz - 1; prePos_ = 0;
    mono_.assign(static_cast<size_t>(std::max(1, maxBlock) + 8), 0.0f); duck_ = 1.0; prepared_ = true; apply(true);
}
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); if (prepared_) apply(false); }

void Processor::apply(bool snap) {
    const int t = std::clamp(static_cast<int>(target_[Type] + 0.5), 0, 2), tone = std::clamp(static_cast<int>(target_[Tone] + 0.5), 0, 2);
    for (int i = 0; i < 8; ++i) fdn_.setLength(i, kBaseMs[i] * 0.001 * fs_ * kScale[t]);
    if (snap) fdn_.snapLengths();
    fdn_.setDecay(target_[Decay]); fdn_.setDamping(kDamp[tone]); fdn_.setModulation(3.0, 0.4);
    preLen_ = target_[PreDelay] * 0.001 * fs_;
    trim_ = 1.0 / std::sqrt(Fdn::kEnergyConstant * target_[Decay] / fdn_.meanLengthSeconds());
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    if (static_cast<size_t>(n) > mono_.size()) mono_.assign(static_cast<size_t>(n), 0.0f);
    const int nc = std::min(numCh, 2);
    for (int i = 0; i < n; ++i) mono_[static_cast<size_t>(i)] = nc > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
    const bool talking = target_[Duck] > 0.5 && vd_.process(mono_.data(), n);
    const double dTarget = talking ? 0.3548133892 : 1.0, aA = std::exp(-1.0 / (0.08 * fs_)), aR = std::exp(-1.0 / (0.5 * fs_));
    for (int i = 0; i < n; ++i) {
        pre_[prePos_ & preMask_] = mono_[static_cast<size_t>(i)];
        const double rp = static_cast<double>(prePos_) - preLen_, fl = std::floor(rp), fr = rp - fl; const size_t i0 = static_cast<size_t>(static_cast<long long>(fl)) & preMask_, i1 = (i0 + 1) & preMask_;
        const double x = preLen_ <= 0.0 ? mono_[static_cast<size_t>(i)] : (1.0 - fr) * pre_[i0] + fr * pre_[i1]; ++prePos_;
        double l, r; fdn_.process(x, l, r);
        const double c = dTarget < duck_ ? aA : aR; duck_ = dTarget + c * (duck_ - dTarget);
        const double g = trim_ * duck_;
        float yl = static_cast<float>(l * g), yr = static_cast<float>(r * g);
        if (!std::isfinite(yl)) yl = 0.0f; if (!std::isfinite(yr)) yr = 0.0f;
        ch[0][i] = std::abs(yl) < 1e-30f ? 0.0f : yl; if (nc > 1) ch[1][i] = std::abs(yr) < 1e-30f ? 0.0f : yr;
    }
}

}  // namespace sw::lv24
