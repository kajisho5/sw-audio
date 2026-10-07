#include "lv15/lv15.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv15 {
namespace {
constexpr double kActive = 0.0031622776601683794;   // -50 dBFS
// the position of the last talker is kept in the shared slots: a = level, b = time since this mic was last active (s, saturating)
}

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv15.mode",     "Mode",          0, 1, 0,    Curve::Step, 1, {0, 1}, "", {"Gain share", "Gate"}},
        {"lv15.lastmic",  "Last mic hold", 0, 1, 1,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"lv15.offatten", "Off atten",     -40, 0, -15, Curve::Lin, 1, {}, "dB"},
        {"lv15.response", "Response",      0, 2, 2,    Curve::Step, 1, {0, 1, 2}, "", {"Slow", "Medium", "Fast"}},
        {"lv15.priority", "Priority",      0, 8, 1,    Curve::Step, 1, {0, 1, 2, 3, 4, 5, 6, 7, 8}, "", {"None", "Mic 1", "Mic 2", "Mic 3", "Mic 4", "Mic 5", "Mic 6", "Mic 7", "Mic 8"}},
        {"lv15.nom",      "NOM limit",     1, 8, 4,    Curve::Step, 1, {1, 2, 3, 4, 5, 6, 7, 8}, "", {"1", "2", "3", "4", "5", "6", "7", "8"}},
    };
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) { fs_ = sampleRate; ms_ = 0; lvl_ = 0; g_ = 1.0; link_.join(); prepared_ = true; }
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    const int nc = std::min(numCh, 2);
    const double a50 = std::exp(-1.0 / (0.05 * fs_));
    for (int i = 0; i < n; ++i) { double p = 0; for (int c = 0; c < nc; ++c) p += static_cast<double>(ch[c][i]) * ch[c][i]; p /= nc; ms_ = a50 * ms_ + (1.0 - a50) * p; }
    lvl_ = std::sqrt(ms_);
    auto* mine = link_.mine();
    if (!mine) return;   // more than 8 instances: this one is not in the mixer
    const bool active = lvl_ > kActive;
    const double dt = static_cast<double>(n) / fs_;
    mine->a.store(static_cast<float>(lvl_));
    // b: time since last active (grows while silent)
    mine->b.store(active ? 0.0f : std::min(1e6f, mine->b.load() + static_cast<float>(dt)));
    mine->flag.store(active);
    link_.tick(); watch_.update(dt);
    // everybody's numbers
    double lv[8]; double since[8]; bool used[8], act[8]; int nUsed = 0;
    for (int i = 0; i < 8; ++i) { auto& s = LinkGroup<LinkTag>::slots()[static_cast<size_t>(i)]; used[i] = watch_.alive(i) || i == link_.index(); lv[i] = used[i] ? s.a.load() : 0.0; since[i] = used[i] ? s.b.load() : 1e9; act[i] = used[i] && s.flag.load(); if (used[i]) ++nUsed; }
    const int me = link_.index(), prio = static_cast<int>(target_[Priority] + 0.5) - 1, nom = std::clamp(static_cast<int>(target_[NomLimit] + 0.5), 1, 8);
    const double off = std::pow(10.0, target_[OffAtten] / 20.0);
    // the open set: the loudest `nom` active mics (the priority mic, when active, always among them)
    bool open[8] = {false, false, false, false, false, false, false, false}; int cnt = 0;
    if (prio >= 0 && prio < 8 && act[prio]) { open[prio] = true; ++cnt; }
    while (cnt < nom) { int best = -1; for (int i = 0; i < 8; ++i) if (act[i] && !open[i] && (best < 0 || lv[i] > lv[best])) best = i; if (best < 0) break; open[best] = true; ++cnt; }
    double target = off;
    if (cnt > 0) {
        if (target_[Mode] > 0.5) target = open[me] ? 1.0 : off;
        else { double sum = 0; for (int i = 0; i < 8; ++i) if (open[i]) sum += lv[i] * (i == prio ? 3.0 : 1.0); target = open[me] ? std::max(off, lv[me] * (me == prio ? 3.0 : 1.0) / sum) : off; }
    } else {   // nobody active
        if (target_[LastMicHold] > 0.5) { int last = -1; for (int i = 0; i < 8; ++i) if (used[i] && (last < 0 || since[i] < since[last])) last = i; target = last == me ? 1.0 : off; }
        else target = target_[Mode] > 0.5 ? off : 1.0 / std::max(1, nUsed);
    }
    const int sp = std::clamp(static_cast<int>(target_[Response] + 0.5), 0, 2);
    static constexpr double kUp[3] = {100, 40, 15}, kDown[3] = {400, 150, 60};
    const double ms = target > g_ ? kUp[sp] : kDown[sp], c = std::exp(-static_cast<double>(n) / (0.001 * ms * fs_));
    const double g0 = g_; g_ = target + c * (g_ - target);
    for (int i = 0; i < n; ++i) {   // linear ramp over the block
        const double g = g0 + (g_ - g0) * (static_cast<double>(i + 1) / n);
        if (g != 1.0) for (int ch_ = 0; ch_ < nc; ++ch_) { const float y = static_cast<float>(ch[ch_][i] * g); ch[ch_][i] = std::abs(y) < 1e-30f ? 0.0f : y; }
    }
}

}  // namespace sw::lv15
