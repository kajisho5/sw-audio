#include "lv11/lv11.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv11 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"lv11.mic",      "Mic",         0, 2, 0,    Curve::Step, 1, {0, 1, 2}, "", {"Live", "Push to talk", "Off"}},
            {"lv11.automute", "Auto mute",   0, 1, 1,    Curve::Step, 1, {0, 1}, "", {"Off", "On silence"}},
            {"lv11.silence",  "Silence",     -70, -30, -48, Curve::Lin, 1, {}, "dB"},
            {"lv11.hold",     "Hold",        0.5, 10, 3, Curve::Lin, 1, {}, "s"},
            {"lv11.fade",     "Fade",        5, 200, 20, Curve::Log, 1, {}, "ms"},
            {"lv11.duck",     "Duck others", -30, 0, -10, Curve::Lin, 1, {}, "dB"},
            {"lv11.cough",    "Hold to cough", 0, 1, 0,  Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        };
        v[HoldToCough].automatable = false;
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate; ms_ = 0; quiet_ = 0; autoMuted_ = false; duckDb_ = 0; link_.join(); prepared_ = true; g_ = wantOpen() ? 1.0 : 0.0;
}
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); }

bool Processor::wantOpen() const {
    const bool held = target_[HoldToCough] > 0.5;
    switch (static_cast<int>(target_[Mic] + 0.5)) {
        case Off: return false;
        case PushToTalk: return held;
        default: return !held && !autoMuted_;
    }
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    const int nc = std::min(numCh, 2);
    const double silenceLin2 = std::pow(10.0, target_[Silence] / 10.0), a100 = std::exp(-1.0 / (0.1 * fs_)), step = 1.0 / (0.001 * target_[Fade] * fs_);
    const bool duckOn = target_[DuckOthers] < -0.01;
    auto* mine = link_.mine();
    watch_.update(static_cast<double>(n) / fs_);
    // others' duck: the deepest amount among the talking ones
    double want = 0.0;
    for (int i = 0; i < 8; ++i) if (i != link_.index()) { auto& s = LinkGroup<LinkTag>::slots()[static_cast<size_t>(i)]; if (watch_.alive(i) && s.flag.load()) want = std::min(want, static_cast<double>(s.a.load())); }
    const double dA = std::exp(-1.0 / (0.01 * fs_)), dR = std::exp(-1.0 / (0.3 * fs_));
    for (int i = 0; i < n; ++i) {
        double p = 0; for (int c = 0; c < nc; ++c) p += static_cast<double>(ch[c][i]) * ch[c][i]; p /= nc;
        ms_ = a100 * ms_ + (1.0 - a100) * p;
        if (target_[AutoMute] > 0.5) {
            if (ms_ < silenceLin2) { quiet_ += 1.0; if (quiet_ >= target_[Hold] * fs_) autoMuted_ = true; }
            else { quiet_ = 0; if (autoMuted_ && ms_ > silenceLin2 * 2.0) autoMuted_ = false; }   // +3 dB
        } else { autoMuted_ = false; quiet_ = 0; }
        const double tg = wantOpen() ? 1.0 : 0.0;
        if (g_ < tg) g_ = std::min(tg, g_ + step); else if (g_ > tg) g_ = std::max(tg, g_ - step);
        const double t = duckDb_ > want ? dA : dR; duckDb_ = want + t * (duckDb_ - want); if (std::abs(duckDb_) < 1e-4 && want == 0.0) duckDb_ = 0.0;
        const double dg = duckDb_ != 0.0 ? std::pow(10.0, duckDb_ / 20.0) : 1.0, gg = g_ * dg;
        if (gg != 1.0) for (int c = 0; c < nc; ++c) { const float y = static_cast<float>(ch[c][i] * gg); ch[c][i] = std::abs(y) < 1e-30f ? 0.0f : y; }
    }
    link_.tick();
    if (mine) { mine->a.store(static_cast<float>(target_[DuckOthers])); mine->flag.store(duckOn && g_ > 0.5 && ms_ > silenceLin2); }
}

}  // namespace sw::lv11
