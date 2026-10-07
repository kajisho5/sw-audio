#include "ut02/ut02.hpp"
#include <algorithm>
#include <cmath>

namespace sw::ut02 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"ut02.listen", "Listen",    0, 4, 1,    Curve::Step, 1, {0, 1, 2, 3, 4}, "", {"Stereo", "Mono", "Side", "Left", "Right"}},
        {"ut02.fold",   "Mono fold", -6, 0, -3,  Curve::Lin, 1, {}, "dB"},
        {"ut02.phone",  "Phone speaker", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"ut02.lowcut", "Low cut",   20, 300, 20, Curve::Log, 1, {}, "Hz", {}, "Off"},
        {"ut02.level",  "Level",     -24, 24, 0, Curve::Lin, 1, {}, "dB"},
    };
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::setFilters() {
    for (auto& c : ch_) {
        c.hp1.setup(Svf::Mode::HighPass, 300.0, fs_, 0.70710678, 0.0); c.hp2.setup(Svf::Mode::HighPass, 300.0, fs_, 0.70710678, 0.0);
        c.bell.setup(Svf::Mode::Bell, 1200.0, fs_, 2.5, 3.0);
        c.low.setup(Svf::Mode::HighPass, std::clamp(target_[LowCut], 20.0, 300.0), fs_, 0.70710678, 0.0);
    }
}

void Processor::prepare(double sampleRate, int) { fs_ = sampleRate; setFilters(); for (auto& c : ch_) { c.hp1.reset(); c.hp2.reset(); c.bell.reset(); c.low.reset(); } prepared_ = true; }

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (prepared_ && id == LowCut) setFilters();
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1) return;
    const bool stereo = numCh > 1;
    const int mode = static_cast<int>(target_[Listen] + 0.5);
    const bool phone = target_[Phone] > 0.5, low = target_[LowCut] > 20.5;
    const double fold = std::pow(10.0, target_[MonoFold] / 20.0), lv = std::pow(10.0, target_[Level] / 20.0);
    if (mode == Stereo && !phone && !low && lv == 1.0) return;   // untouched, bit for bit
    for (int i = 0; i < n; ++i) {
        double l = ch[0][i], r = stereo ? ch[1][i] : l;
        switch (mode) {
            case Mono: l = r = (l + r) * fold; break;
            case Side:  l = r = 0.5 * (l - r); break;
            case Left:  r = l; break;
            case Right: l = r; break;
            default: break;
        }
        double o[2] = {l, r};
        for (int c = 0; c < (stereo ? 2 : 1); ++c) {
            auto& f = ch_[static_cast<size_t>(c)]; double y = o[c];
            if (phone) y = f.bell.process(f.hp2.process(f.hp1.process(y)));
            if (low) y = f.low.process(y);
            y *= lv;
            if (!std::isfinite(y) || std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::ut02
