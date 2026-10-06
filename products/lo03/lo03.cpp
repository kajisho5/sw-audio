#include "lo03/lo03.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lo03 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
        {"lo03.role",      "Role",       0, 2, 2,      Curve::Step, 1, {0, 1, 2}, "", {"Kick", "Bass", "Both"}},
        {"lo03.focus",     "Focus",      30, 120, 55,  Curve::Log,  1, {}, "Hz"},
        {"lo03.tight",     "Tight",      0, 100, 50,   Curve::Lin,  1, {}, "%"},
        {"lo03.mudcut",    "Mud cut",    150, 500, 250, Curve::Log, 1, {}, "Hz"},
        {"lo03.monobelow", "Mono below", 20, 300, 120, Curve::Log,  1, {}, "Hz"},
        };
        v[MonoBelow].minLabel = "Off";
        return v;
    }();
    return s;
}
Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }
namespace {
constexpr int kCtl = 32;                 // control block: the Focus bell is re-aimed every 32 samples
constexpr double kTailDepth[3] = {9.0, 0.0, 6.0}, kDuckDepth[3] = {0.0, 6.0, 4.0};   // dB at Tight 100 %, by Role (design values)
constexpr double kMudDepth = 6.0;       // dB at Tight 100 %
constexpr double kDeadZone = 0.25, kOnsetRatio = 2.2, kKeyMin = 1e-3;
}

void Processor::updateStatic(bool ramp) {
    const double mud = -kMudDepth * target_[Tight] * 0.01;
    for (auto& c : c_) {
        if (ramp) c.mud.setupRamp(Svf::Mode::Bell, target_[MudCut], fs_, 1.0, mud, static_cast<int>(0.01 * fs_));
        else c.mud.setup(Svf::Mode::Bell, target_[MudCut], fs_, 1.0, mud);
    }
}

void Processor::updateMono() {
    monoOn_ = target_[MonoBelow] > specs()[MonoBelow].min * 1.0001;
    const double f = target_[MonoBelow];
    sideHp_.setup(Svf::Mode::HighPass, f, fs_); midLp_.setup(Svf::Mode::LowPass, f, fs_); midHp_.setup(Svf::Mode::HighPass, f, fs_);
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    det_.setup(Svf::Mode::BandPass, target_[Focus], fs_, 1.4, 0);
    key_.setup(Svf::Mode::LowPass, 120.0, fs_, 0.70710678, 0);
    for (auto& c : c_) { c.focus.setup(Svf::Mode::Bell, target_[Focus], fs_, 1.2, 0.0); c.focus.reset(); c.mud.reset(); }
    updateStatic(false);
    updateMono();
    e_ = ref_ = kFast_ = kSlow_ = duck_ = duckSm_ = focusDb_ = 0;
    eRel_ = std::exp(-1.0 / (0.04 * fs_)); refDec_ = std::exp(-1.0 / (0.5 * fs_)); duckDec_ = std::exp(-1.0 / (0.05 * fs_)); kRel_ = std::exp(-1.0 / (0.015 * fs_));
    ctl_ = 0;
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (!prepared_) return;
    if (id == Focus) det_.setup(Svf::Mode::BandPass, v, fs_, 1.4, 0);
    if (id == Tight || id == MudCut) updateStatic(true);
    if (id == MonoBelow) updateMono();
}

void Processor::snapToTargets() { if (prepared_) { updateStatic(false); updateMono(); } }

void Processor::run(float** ch, int numCh, int n, const float* const* sc, int scCh) {
    const int nch = std::min(numCh, 2);
    const int role = static_cast<int>(target_[Role] + 0.5);
    const double t = target_[Tight] * 0.01;
    const double tailDepth = kTailDepth[role] * t;
    const bool ext = sc != nullptr && scCh > 0;
    const bool ducking = kDuckDepth[role] > 0.0 && (role == Both || ext);
    const double duckDepth = kDuckDepth[role] * t;
    const double duckAtt = 1.0 - std::exp(-1.0 / ((role == Both ? 0.004 : 0.001) * fs_));
    const bool mudOn = t > 0.0;
    const double focusHz = target_[Focus];
    for (int i = 0; i < n; ++i) {
        double m = 0.0;
        for (int c = 0; c < nch; ++c) m += ch[c][i];
        m /= nch;
        // Focus band follower: level vs its recent peak -> how far the tail has fallen
        const double d = std::abs(det_.process(m));
        e_ = d > e_ ? d : e_ * eRel_;
        ref_ = std::max(e_, ref_ * refDec_);
        const double tail = ref_ > 1e-4 ? std::clamp((1.0 - e_ / (ref_ + 1e-12) - kDeadZone) / (1.0 - kDeadZone), 0.0, 1.0) : 0.0;
        // kick onsets in the key (the sidechain, or the track itself for Both)
        double k = m;
        if (ext && role == Bass) { k = 0.0; for (int c = 0; c < scCh && c < 2; ++c) k += sc[c][i]; k /= std::min(scCh, 2); }
        const double kl = std::abs(key_.process(k));
        kFast_ = kl > kFast_ ? kl : kFast_ * kRel_;
        kSlow_ += (kl - kSlow_) * (1.0 / (0.12 * fs_));
        if (ducking && kFast_ > kKeyMin && kFast_ > kOnsetRatio * kSlow_) duck_ = 1.0; else duck_ *= duckDec_;
        duckSm_ = duck_ > duckSm_ ? duckSm_ + (duck_ - duckSm_) * duckAtt : duck_;
        if (++ctl_ >= kCtl) {
            ctl_ = 0;
            focusDb_ = -std::min(12.0, tail * tailDepth + (ducking ? duckSm_ * duckDepth : 0.0));
            for (int c = 0; c < nch; ++c) c_[static_cast<size_t>(c)].focus.setupRamp(Svf::Mode::Bell, focusHz, fs_, 1.2, focusDb_, kCtl);
        }
        const bool active = mudOn || focusDb_ < -1e-3 || t > 0.0;
        for (int c = 0; c < nch; ++c) {
            double y = ch[c][i];
            if (active) y = c_[static_cast<size_t>(c)].mud.process(c_[static_cast<size_t>(c)].focus.process(y));
            ch[c][i] = static_cast<float>(y);
        }
        if (monoOn_ && nch == 2) {
            const double l = ch[0][i], r = ch[1][i];
            double mid = 0.5 * (l + r), side = 0.5 * (l - r);
            side = sideHp_.process(side);
            mid = midLp_.process(mid) + midHp_.process(mid);
            ch[0][i] = static_cast<float>(mid + side); ch[1][i] = static_cast<float>(mid - side);
        }
        for (int c = 0; c < nch; ++c) if (std::abs(ch[c][i]) < 1e-30f) ch[c][i] = 0.0f;
    }
}

}  // namespace sw::lo03
