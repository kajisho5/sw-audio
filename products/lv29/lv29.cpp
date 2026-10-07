#include "lv29/lv29.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv29 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv29.output",   "Output",       0, 2, 1,    Curve::Step, 1, {0, 1, 2}, "", {"Floor", "Interp and floor", "Interp"}},
        {"lv29.floor",    "Floor under",  -40, 0, -14, Curve::Lin, 1, {}, "dB"},
        {"lv29.xfade",    "Crossfade",    50, 2000, 400, Curve::Log, 1, {}, "ms"},
        {"lv29.interp",   "Interp level", -20, 10, 0, Curve::Lin, 1, {}, "dB"},
        {"lv29.auto",     "Auto detect",  0, 1, 1,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int maxBlock) {
    fs_ = sampleRate; vd_.prepare(fs_); mono_.assign(static_cast<size_t>(std::max(1, maxBlock) + 8), 0.0f); speaking_ = false; prepared_ = true; gF_ = goalFloor(); gI_ = goalInterp();
}
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); }

double Processor::goalFloor() const {
    switch (static_cast<int>(target_[Output] + 0.5)) {
        case FloorOnly: return 1.0;
        case InterpOnly: return 0.0;
        default: { const double under = std::pow(10.0, target_[FloorUnder] / 20.0); return target_[AutoDetect] > 0.5 ? (speaking_ ? under : 1.0) : under; }
    }
}
double Processor::goalInterp() const { return static_cast<int>(target_[Output] + 0.5) == FloorOnly ? 0.0 : std::pow(10.0, target_[InterpLevel] / 20.0); }

void Processor::processWithSidechain(float** ch, int numCh, int n, const float* const* sc, int scCh) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    const int nc = std::min(numCh, 2); const bool hasInterp = sc && scCh > 0 && sc[0];
    if (!hasInterp) return;   // nothing to mix: the floor passes
    if (static_cast<size_t>(n) > mono_.size()) mono_.assign(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i) mono_[static_cast<size_t>(i)] = scCh > 1 && sc[1] ? 0.5f * (sc[0][i] + sc[1][i]) : sc[0][i];
    speaking_ = vd_.process(mono_.data(), n);
    const double a = 1.0 - std::exp(-3.0 / (0.001 * target_[Crossfade] * fs_)), gfT = goalFloor(), giT = goalInterp();
    for (int i = 0; i < n; ++i) {
        gF_ += a * (gfT - gF_); gI_ += a * (giT - gI_);
        for (int c = 0; c < nc; ++c) { const float y = static_cast<float>(ch[c][i] * gF_ + mono_[static_cast<size_t>(i)] * gI_); ch[c][i] = std::abs(y) < 1e-30f ? 0.0f : y; }
    }
}

}  // namespace sw::lv29
