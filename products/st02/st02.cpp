#include "st02/st02.hpp"
#include <algorithm>
#include <cmath>

namespace sw::st02 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"st02.midlevel",  "Mid level",  -12, 12, 0, Curve::Lin, 1, {}, "dB"},
        {"st02.sidelevel", "Side level", -12, 12, 0, Curve::Lin, 1, {}, "dB"},
        {"st02.sidehpf",   "Side HPF",   20, 500, 20, Curve::Log, 1, {}, "Hz", {}, "Off"},
        {"st02.sideair",   "Side air",   0, 10, 0,   Curve::Lin, 1, {}, ""},
        {"st02.midlow",    "Mid low",    -6, 6, 0,   Curve::Lin, 1, {}, "dB"},
        {"st02.encode",    "Encode",     0, 1, 0,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            unitSpec("st02.unit"),
    };
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::update() {
    if (target_[SideHpf] != hpfHz_) { hpf_.setup(Svf::Mode::HighPass, std::max(target_[SideHpf], 20.0), fs_, 0.7071, 0.0); hpfHz_ = target_[SideHpf]; }
    const double air = target_[SideAir] * 0.6;   // 0 .. 10 -> 0 .. +6 dB
    if (air != airDb_) { air_.setup(Svf::Mode::HighShelf, 10000.0, fs_, 0.7071, air); airDb_ = air; }
    if (target_[MidLow] != lowDb_) { low_.setup(Svf::Mode::LowShelf, 100.0, fs_, 0.7071, target_[MidLow]); lowDb_ = target_[MidLow]; }
    midT_ = std::pow(10.0, target_[MidLevel] / 20.0); sideT_ = std::pow(10.0, target_[SideLevel] / 20.0);
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    hpf_.reset(); air_.reset(); low_.reset();
    hpfHz_ = airDb_ = lowDb_ = -99.0;
    update(); midG_ = midT_; sideG_ = sideT_;
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::snapToTargets() { if (prepared_) { update(); midG_ = midT_; sideG_ = sideT_; } }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 2) return;
    update();
    const bool enc = target_[Encode] > 0.5, hpOn = target_[SideHpf] > 20.0 * 1.0001;
    const double k = 1.0 - std::exp(-1.0 / (0.01 * fs_));
    for (int i = 0; i < n; ++i) {
        midG_ += k * (midT_ - midG_); sideG_ += k * (sideT_ - sideG_);
        double m, s;
        if (enc) { m = ch[0][i]; s = ch[1][i]; } else { m = 0.5 * (ch[0][i] + ch[1][i]); s = 0.5 * (ch[0][i] - ch[1][i]); }
        m = low_.process(m) * midG_;
        if (hpOn) s = hpf_.process(s);
        if (target_[SideAir] > 0.0) s = air_.process(s);
        s *= sideG_;
        double a, b;
        if (enc) { a = m; b = s; } else { a = m + s; b = m - s; }
        if (std::abs(a) < 1e-30) a = 0.0;
        if (std::abs(b) < 1e-30) b = 0.0;
        ch[0][i] = static_cast<float>(a); ch[1][i] = static_cast<float>(b);
    }
}

}  // namespace sw::st02
