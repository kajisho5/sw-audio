#include "lv05/lv05.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv05 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"lv05.depth",   "Depth",        -40, 0, -12,   Curve::Lin, 1, {}, "dB"},
            {"lv05.attack",  "Attack",       1, 500, 80,    Curve::Log, 1, {}, "ms"},
            {"lv05.hold",    "Hold",         0, 5, 1.2,     Curve::Lin, 1, {}, "s"},
            {"lv05.release", "Release",      0.1, 10, 2.0,  Curve::Log, 1, {}, "s"},
            {"lv05.voice",   "Voice only",   0, 1, 1,       Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"lv05.hold2duck", "Hold to duck", 0, 1, 0,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        };
        v[HoldToDuck].automatable = false;
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int maxBlock) {
    fs_ = sampleRate; vd_.prepare(fs_); gDb_ = 0; env_ = 0; floor_ = 1e-4; holdLeft_ = 0; keyOn_ = false;
    mono_.assign(static_cast<size_t>(std::max(1, maxBlock) + 8), 0.0f); prepared_ = true;
}
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); }

void Processor::run(float** ch, int numCh, int n, const float* const* sc, int scCh) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    if (static_cast<size_t>(n) > mono_.size()) mono_.assign(static_cast<size_t>(n), 0.0f);   // only if the host exceeds the announced block size
    const bool hasKey = sc && scCh > 0 && sc[0];
    const bool forced = target_[HoldToDuck] > 0.5;
    if (hasKey) for (int i = 0; i < n; ++i) mono_[static_cast<size_t>(i)] = scCh > 1 && sc[1] ? 0.5f * (sc[0][i] + sc[1][i]) : sc[0][i];
    // key decision per block of samples (voice detector runs on the key, frames of 10 ms)
    const double depth = std::min(0.0, target_[Depth]);
    const double aC = std::exp(-1.0 / (0.001 * target_[Attack] * fs_)), rC = std::exp(-1.0 / (target_[Release] * fs_)), holdN = target_[Hold] * fs_;
    const double envC = std::exp(-1.0 / (0.01 * fs_));
    const bool voiceOnly = target_[VoiceOnly] > 0.5;
    // the detector is fed in 64-sample pieces so that the decision follows within about 1.3 ms of a frame boundary
    for (int off = 0; off < n; off += 64) {
        const int m = std::min(64, n - off);
        bool on = false;
        if (hasKey) {
            vd_.process(mono_.data() + off, m);
            if (voiceOnly) on = vd_.active();
            else { double pk = 0; for (int i = 0; i < m; ++i) { const double a = std::abs(mono_[static_cast<size_t>(off + i)]); env_ = std::max(a, envC * env_); pk = std::max(pk, env_); }
                   if (pk < floor_) floor_ = std::max(1e-6, pk); else floor_ = std::min(floor_ * std::pow(10.0, 3.0 / 20.0 * m / fs_), std::max(floor_, 0.0056));
                   on = pk > std::max(0.00316, floor_ * 3.1623); }
        }
        keyOn_ = on || forced;
        if (keyOn_) holdLeft_ = holdN;
        const bool ducking = keyOn_ || holdLeft_ > 0;
        if (!keyOn_ && holdLeft_ > 0) holdLeft_ -= m;
        for (int i = 0; i < m; ++i) {
            const double target = ducking ? depth : 0.0;
            const double c = target < gDb_ ? aC : rC;
            gDb_ = target + c * (gDb_ - target);
            if (std::abs(gDb_) < 1e-4 && target == 0.0) gDb_ = 0.0;
            if (gDb_ != 0.0) { const double g = std::pow(10.0, gDb_ / 20.0); for (int c2 = 0; c2 < numCh; ++c2) { const float y = static_cast<float>(ch[c2][off + i] * g); ch[c2][off + i] = std::abs(y) < 1e-30f ? 0.0f : y; } }
        }
    }
}

}  // namespace sw::lv05
