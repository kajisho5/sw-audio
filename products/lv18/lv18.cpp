#include "lv18/lv18.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv18 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv18.sens",     "Sensitivity", 0, 2, 2,    Curve::Step, 1, {0, 1, 2}, "", {"Low", "Mid", "High"}},
        {"lv18.mute",     "Mute time",   10, 200, 40, Curve::Log, 1, {}, "ms"},
        {"lv18.plug",     "Plug pop",    0, 1, 1,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"lv18.wind",     "Wind",        0, 1, 1,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"lv18.handling", "Handling",    0, 1, 1,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"lv18.plosive",  "Plosive",     0, 1, 0,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; count_.fill(0); }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& c : hp_) for (auto& f : c) f.setup(Svf::Mode::HighPass, 200.0, fs_, 0.70710678, 0);
    lp30_ = avg30_ = lp300_ = lp150_ = lp50_ = lp50b_ = eL50_ = eFast_ = eLow_ = eBg_ = eL150_ = eH_ = eL150bg_ = peak_ = 0;
    muteLeft_ = hpLeft_ = windOn_ = windOff_ = refractory_ = 0; windActive_ = false; mg_ = 1.0; hpMix_ = 0.0; prepared_ = true;
}
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    const int nc = std::min(numCh, 2);
    static constexpr double kScale[3] = {1.6, 1.0, 0.6};
    const double sc = kScale[std::clamp(static_cast<int>(target_[Sensitivity] + 0.5), 0, 2)];
    auto rate = [&](double hz) { return 1.0 - std::exp(-2.0 * 3.14159265358979323846 * hz / fs_); };
    const double k30 = rate(30.0), k300 = rate(300.0), k150 = rate(150.0), k50 = rate(50.0);
    const double aF = std::exp(-1.0 / (0.002 * fs_)), aBg = std::exp(-1.0 / (0.1 * fs_)), a200 = std::exp(-1.0 / (0.2 * fs_)), aS = std::exp(-1.0 / (0.005 * fs_));
    const double muteN = 0.001 * target_[MuteTime] * fs_, fadeDown = 1.0 / (0.003 * fs_), fadeUp = 1.0 / (0.02 * fs_), hpStep = 1.0 / (0.01 * fs_);
    const bool onPlug = target_[PlugPop] > 0.5, onWind = target_[Wind] > 0.5, onHand = target_[Handling] > 0.5, onPlos = target_[Plosive] > 0.5;
    for (int i = 0; i < n; ++i) {
        double m = 0; for (int c = 0; c < nc; ++c) m += ch[c][i]; m /= nc;
        lp30_ += k30 * (m - lp30_); lp300_ += k300 * (m - lp300_); lp150_ += k150 * (m - lp150_); lp50_ += k50 * (m - lp50_);
        const double hi = m - lp300_;
        // slow averages / fast energies
        avg30_ = a200 * avg30_ + (1 - a200) * std::abs(lp30_);
        eFast_ = aF * eFast_ + (1 - aF) * m * m; eLow_ = aF * eLow_ + (1 - aF) * lp300_ * lp300_; if (eFast_ <= 3.0 * eBg_ || eBg_ < 1e-8) eBg_ = aBg * eBg_ + (1 - aBg) * m * m;   // the background stands still while an event is on
                eL150_ = aF * eL150_ + (1 - aF) * lp150_ * lp150_; eH_ = aF * eH_ + (1 - aF) * hi * hi; if (eL150_ <= 3.0 * eL150bg_ || eL150bg_ < 1e-9) eL150bg_ = aBg * eL150bg_ + (1 - aBg) * lp150_ * lp150_;
        lp50b_ += k50 * (lp50_ - lp50b_); eL50_ = aBg * eL50_ + (1 - aBg) * lp50b_ * lp50b_;
        peak_ = std::max(std::abs(m), peak_ * aS);
        if (refractory_ > 0) --refractory_;
        else {
            if (onPlug && std::abs(lp30_) > 0.05 * sc && std::abs(lp30_) > 3.0 * avg30_) { muteLeft_ = muteN; ++count_[EvPlug]; refractory_ = muteN + 0.05 * fs_; }
            else if (onHand && eFast_ > 1e-6 && eFast_ > 100.0 * eBg_ && eFast_ > 1e-4 * (sc > 1 ? 4.0 : sc < 1 ? 0.25 : 1.0) && eLow_ > 0.7 * eFast_ && peak_ > 0.1) { muteLeft_ = muteN; ++count_[EvHandling]; refractory_ = muteN + 0.05 * fs_; }
            else if (onPlos && eL150_ > 1e-5 / (sc * sc) && eL150_ > 63.0 * eL150bg_ && eH_ < 0.1 * eL150_) { hpLeft_ = std::max(hpLeft_, muteN); ++count_[EvPlosive]; refractory_ = muteN + 0.05 * fs_; }
        }
        // wind: a long condition
        const bool windCond = onWind && eL50_ > 1.6e-4 / (sc * sc);
        if (windCond) { windOn_ += 1; windOff_ = 0; } else { windOff_ += 1; if (!windActive_) windOn_ = 0; }
        if (!windActive_ && windOn_ > 0.15 * fs_) { windActive_ = true; ++count_[EvWind]; }
        if (windActive_ && windOff_ > 0.3 * fs_) { windActive_ = false; windOn_ = 0; }
        // reactions
        const double mt = muteLeft_ > 0 ? 0.0 : 1.0;
        if (mg_ > mt) mg_ = std::max(mt, mg_ - fadeDown); else if (mg_ < mt) mg_ = std::min(mt, mg_ + fadeUp);
        if (muteLeft_ > 0) --muteLeft_;
        const double ht = (hpLeft_ > 0 || windActive_) ? 1.0 : 0.0;
        if (hpMix_ < ht) hpMix_ = std::min(ht, hpMix_ + hpStep); else if (hpMix_ > ht) hpMix_ = std::max(ht, hpMix_ - hpStep * 0.25);
        if (hpLeft_ > 0) --hpLeft_;
        for (int c = 0; c < nc; ++c) {
            double y = ch[c][i];
            const double h = hp_[static_cast<size_t>(c)][1].process(hp_[static_cast<size_t>(c)][0].process(y));
            if (hpMix_ > 0.0) y = y + hpMix_ * (h - y); else (void)h;
            y *= mg_;
            const float o = static_cast<float>(y); ch[c][i] = std::abs(o) < 1e-30f ? 0.0f : o;
        }
    }
}

}  // namespace sw::lv18
