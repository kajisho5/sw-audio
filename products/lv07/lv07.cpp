#include "lv07/lv07.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv07 {
namespace {
constexpr int kKeep = 150;   // frames of 20 ms = 3 s
}

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv07.use",    "Use",       0, 2, 0,    Curve::Step, 1, {0, 1, 2}, "", {"Speech", "Panel", "Lecture"}},
        {"lv07.target", "Target",    -30, -10, -18, Curve::Lin, 1, {}, "LUFS"},
        {"lv07.maxgain", "Max gain", 0, 24, 12,  Curve::Lin, 1, {}, "dB"},
        {"lv07.speed",  "Speed",     0, 2, 1,    Curve::Step, 1, {0, 1, 2}, "", {"Slow", "Medium", "Fast"}},
        {"lv07.gate",   "Gate",      -70, -30, -50, Curve::Lin, 1, {}, "dB"},
        {"lv07.hold",   "Talker hold", 0, 1, 1,  Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"lv07.freeze", "Freeze",    0, 1, 0,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate; meter_.setup(fs_, 2); gDb_ = 0; lim_ = 1; rms_ = 0; dist_ = 0; shelfDb_ = 0; seen_ = 0; tick_ = 0;
    for (size_t c = 0; c < 2; ++c) {
        hi_[c].setup(Svf::Mode::HighPass, 3000.0, fs_, 0.70710678, 0);
        mid_[c].setup(Svf::Mode::BandPass, 1000.0, fs_, 0.5, 0);
        pres_[c].setup(Svf::Mode::HighShelf, 3000.0, fs_, 0.70710678, 0);
    }
    eHi_ = eMid_ = frameE_ = 0; frameN_ = 0; frameLen_ = std::max(1, static_cast<int>(std::lround(0.02 * fs_))); nFrames_ = 0; head_ = 0;
    frames_.assign(kKeep, 0.0f); scratch_.reserve(kKeep); hiMid_ = 0; hiMidN_ = 0; frameMax_ = -120; prepared_ = true;
}
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    const bool stereo = numCh > 1;
    { const float* in[2] = {ch[0], stereo ? ch[1] : ch[0]}; meter_.process(in, 2, n); }
    // gate level and the near / far features (mono sum, per sample)
    double blockPow = 0;
    for (int i = 0; i < n; ++i) {
        const double m = stereo ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i];
        blockPow += m * m;
        const double h = hi_[0].process(m), b = mid_[0].process(m);
        eHi_ += h * h; eMid_ += b * b; frameE_ += m * m;
        if (++frameN_ >= frameLen_) {
            frames_[static_cast<size_t>(head_)] = static_cast<float>(10.0 * std::log10(frameE_ / frameN_ + 1e-12)); head_ = (head_ + 1) % kKeep; nFrames_ = std::min(nFrames_ + 1, kKeep);
            const double fdb = 10.0 * std::log10(frameE_ / frameN_ + 1e-12); frameMax_ = std::max(fdb, frameMax_ - 0.01);
            if (fdb > target_[Gate] + 6.0 && fdb > frameMax_ - 12.0) { hiMid_ += 10.0 * std::log10((eHi_ + 1e-18) / (eMid_ + 1e-18)); ++hiMidN_; }
            eHi_ = eMid_ = frameE_ = 0; frameN_ = 0;
            if (hiMidN_ > 300) { hiMid_ *= 0.5; hiMidN_ /= 2; }
        }
    }
    rms_ = 10.0 * std::log10(blockPow / n + 1e-12);
    seen_ += n; tick_ += static_cast<unsigned>(n);
    if (tick_ >= static_cast<unsigned>(0.1 * fs_)) {   // near / far, every ~100 ms
        const double dt = static_cast<double>(tick_) / fs_; tick_ = 0;
        scratch_.clear(); for (int i = 0; i < nFrames_; ++i) if (frames_[static_cast<size_t>(i)] > -90.0f) scratch_.push_back(frames_[static_cast<size_t>(i)]);
        if (scratch_.size() >= 60 && hiMidN_ >= 20) {
            const size_t k10 = scratch_.size() / 10, k90 = scratch_.size() * 9 / 10;
            std::nth_element(scratch_.begin(), scratch_.begin() + static_cast<long>(k10), scratch_.end()); const double p10 = scratch_[k10];
            std::nth_element(scratch_.begin(), scratch_.begin() + static_cast<long>(k90), scratch_.end()); const double p90 = scratch_[k90];
            const double dSpread = std::clamp((25.0 - (p90 - p10)) / 13.0, 0.0, 1.0), dRatio = std::clamp((-26.0 - hiMid_ / hiMidN_) / 12.0, 0.0, 1.0);
            const double tgt = 0.5 * (dSpread + dRatio), a = 1.0 - std::exp(-dt / 3.0); dist_ += a * (tgt - dist_);
        }
    }
    const double sh = 4.0 * dist_;
    if (std::abs(sh - shelfDb_) > 0.1) { shelfDb_ = sh; for (auto& f : pres_) f.setup(Svf::Mode::HighShelf, 3000.0, fs_, 0.70710678, shelfDb_); }
    // gain
    const double mom = meter_.momentary(), gateOn = rms_ > target_[Gate];
    const double useF[3] = {1.0, 1.5, 0.6}, up[3] = {2, 6, 15}, down[3] = {6, 12, 30};
    const int sp = std::clamp(static_cast<int>(target_[Speed]), 0, 2), us = std::clamp(static_cast<int>(target_[Use]), 0, 2);
    const double stepUp = up[sp] * useF[us] * n / fs_, stepDown = down[sp] * useF[us] * n / fs_;
    if (target_[Freeze] < 0.5 && seen_ > 0.4 * fs_) {
        if (gateOn && mom > -70.0) { const double want = std::clamp(target_[Target] - mom, -12.0, target_[MaxGain] + 3.0 * dist_); gDb_ += std::clamp(want - gDb_, -stepDown, stepUp); }
        else if (target_[TalkerHold] < 0.5) { const double st = 3.0 * n / fs_; gDb_ += std::clamp(0.0 - gDb_, -st, st); }
    }
    const double g = std::pow(10.0, gDb_ / 20.0), ceil = 0.89125093813374556, relC = std::exp(-1.0 / (0.08 * fs_));
    for (int i = 0; i < n; ++i) {
        double y[2] = {ch[0][i] * g, stereo ? ch[1][i] * g : 0.0};
        if (shelfDb_ > 0.05) { y[0] = pres_[0].process(y[0]); if (stereo) y[1] = pres_[1].process(y[1]); }
        double pk = std::abs(y[0]); if (stereo) pk = std::max(pk, std::abs(y[1]));
        const double need = pk > ceil ? ceil / pk : 1.0;
        lim_ = need < lim_ ? need : relC * lim_ + (1 - relC) * need;
        if (lim_ * pk > ceil) lim_ = ceil / pk;
        for (int c = 0; c < (stereo ? 2 : 1); ++c) { const float o = static_cast<float>(y[c] * lim_); ch[c][i] = std::abs(o) < 1e-30f ? 0.0f : o; }
    }
}

}  // namespace sw::lv07
