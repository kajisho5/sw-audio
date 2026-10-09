#include "ms01/ms01.hpp"
#include <algorithm>
#include <cmath>

namespace sw::ms01 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"ms01.gain",     "Gain",        0, 24, 0,       Curve::Lin,  1, {}, "dB"},
            {"ms01.target",   "Target",      -30, -5, -14,   Curve::Lin,  1, {}, "LUFS"},
            {"ms01.evo.on",   "Lock",        0, 1, 0,        Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"ms01.char.x",   "Character X", 0, 100, 50,     Curve::Lin,  1, {}, ""},
            {"ms01.char.y",   "Character Y", 0, 100, 50,     Curve::Lin,  1, {}, ""},
            {"ms01.ceiling",  "Ceiling",     -12, 0, -1,     Curve::Lin,  1, {}, "dBTP"},
            {"ms01.release",  "Release",     1, 1000, 1000,  Curve::Skew, 3, {}, "ms"},
            {"ms01.stereo",   "Stereo",      0, 100, 100,    Curve::Lin,  1, {}, "%"},
            {"ms01.tp",       "True peak",   0, 1, 1,        Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"ms01.dither",   "Dither",      0, 24, 0,       Curve::Step, 1, {0, 16, 24}, "", {"Off", "16 bit", "24 bit"}},
            {"ms01.lowguard", "Low end guard", 0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"ms01.lowlat",   "Low lat",     0, 1, 0,        Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        };
        v[Release].maxLabel = "Auto";
        return v;
    }();
    return s;
}

namespace {
constexpr double kLookaheadMs = 2.0, kLowLookaheadMs = 0.5, kSlowOffsetDb = 6.0, kGuardHz = 120.0, kGuardDb = -12.0;
constexpr double kLockTauS = 10.0, kLockMinS = 30.0, kLockWithinLu = 0.3, kLockWarmS = 2.0, kForgetS = 10.0, kLockSteadyLu = 0.15;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

int Processor::latencySamples() const {
    const int la = std::max(1, static_cast<int>(std::lround((target_[LowLat] > 0.5 ? kLowLookaheadMs : kLookaheadMs) * 0.001 * fs_)));   // Low lat: 0.5 ms (for the next prepare)
    return la + (target_[TruePeak] > 0.5 ? TruePeakDetector::kTapsPerPhase : 0);
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    const int la = std::max(1, static_cast<int>(std::lround((target_[LowLat] > 0.5 ? kLowLookaheadMs : kLookaheadMs) * 0.001 * fs_)));
    lim_.prepare(fs_, 2, la, target_[TruePeak] > 0.5, 4);
    gainDb_ = target_[Gain];
    gain_.reset(fs_, 20.0, std::pow(10.0, gainDb_ / 20.0));
    envC_ = Ballistics::coef(fs_, 10.0);
    for (auto& g : guard_) g.setup(Svf::Mode::LowShelf, kGuardHz, fs_, 0.70710678, kGuardDb);
    meter_.setup(fs_, 2, kForgetS);
    env_ = {0, 0}; slowGr_ = 0;
    for (auto& b : slowBall_) b.reset(0.0);
    restartLock();
    applyLimiter(); applySlow();
}

void Processor::restartLock() { locked_ = false; lockClock_ = 0; lockTick_ = 0; histClock_ = 0; histPos_ = histFill_ = 0; meter_.reset(); }

void Processor::applyLimiter() {
    const bool autoRel = target_[Release] >= specs()[Release].max;
    lim_.set(target_[Ceiling] - 0.02, target_[Release], target_[Stereo] / 100.0);
    if (autoRel) lim_.setAutoRelease(40.0, 400.0);   // Auto: short reductions recover fast (40 ms), sustained ones slowly (400 ms)
}

void Processor::applySlow() {
    const double x = target_[CharX] / 100.0, y = target_[CharY] / 100.0;
    slow_.set(target_[Ceiling] - kSlowOffsetDb, 1.0 + 3.0 * x, 12.0 * x);
    const bool autoRel = target_[Release] >= specs()[Release].max;
    const double rel = autoRel ? 300.0 : std::clamp(3.0 * target_[Release], 20.0, 1000.0);
    for (auto& b : slowBall_) b.set(fs_, std::pow(30.0, y), rel);   // attack 1 ms (Smooth) .. 30 ms (Punch)
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case Gain: if (target_[Lock] < 0.5) { gainDb_ = v; gain_.setTarget(std::pow(10.0, v / 20.0)); } break;
        case Lock: if (v > 0.5) { gainDb_ = target_[Gain]; gain_.setTarget(std::pow(10.0, gainDb_ / 20.0)); } restartLock(); break;
        case Target: restartLock(); break;
        case Ceiling: case Release: case Stereo: applyLimiter(); applySlow(); break;
        case CharX: case CharY: applySlow(); break;
        default: break;  // TruePeak: next prepare
    }
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const double link = target_[Stereo] / 100.0;
    const bool guard = target_[LowGuard] > 0.5, lockOn = target_[Lock] > 0.5;
    for (int i = 0; i < n; ++i) {
        const double g = gain_.next();
        double det[2] = {0, 0}, lv[2] = {-200, -200}, x[2] = {0, 0};
        for (int c = 0; c < nch; ++c) {
            x[c] = ch[c][i] * g;
            det[c] = guard ? guard_[static_cast<size_t>(c)].process(x[c]) : x[c];
            env_[static_cast<size_t>(c)] = std::max(std::abs(det[c]), envC_ * env_[static_cast<size_t>(c)]);
            lv[c] = 20.0 * std::log10(std::max(env_[static_cast<size_t>(c)], 1e-9));
        }
        const double common = nch > 1 ? std::max(lv[0], lv[1]) : lv[0];
        for (int c = 0; c < nch; ++c) {
            const double own = slow_.gainDb(lv[c]), all = slow_.gainDb(common);
            const double gr = slowBall_[static_cast<size_t>(c)].process(own + link * (all - own));
            if (c == 0) slowGr_ = gr;
            ch[c][i] = static_cast<float>(x[c] * std::pow(10.0, gr / 20.0));
        }
    }
    lim_.process(ch, nch, n);
    if (lockOn) {   // measure the output, move Gain toward Target every 100 ms
        const float* c2[2] = {ch[0], nch > 1 ? ch[1] : ch[0]};
        meter_.process(c2, 2, n);
        lockClock_ += n / fs_; lockTick_ += n / fs_;
        while (lockTick_ >= 0.1) {
            lockTick_ -= 0.1;
            const double lufs = meter_.integrated();
            if (locked_ || lufs < -100.0 || lockClock_ < kLockWarmS) continue;
            histClock_ += 0.1;
            if (histClock_ >= 1.0) { histClock_ -= 1.0; hist_[static_cast<size_t>(histPos_)] = lufs; histPos_ = (histPos_ + 1) % 6; histFill_ = std::min(histFill_ + 1, 6); }
            const double hi = *std::max_element(hist_.begin(), hist_.begin() + histFill_), lo = *std::min_element(hist_.begin(), hist_.begin() + histFill_);
            const bool steady = histFill_ >= 6 && hi - lo < kLockSteadyLu;
            const double e = target_[Target] - lufs;
            gainDb_ = std::clamp(gainDb_ + e * 0.1 / kLockTauS, 0.0, 24.0);
            gain_.setTarget(std::pow(10.0, gainDb_ / 20.0));
            if (lockClock_ >= kLockMinS && std::abs(e) < kLockWithinLu && steady) locked_ = true;
        }
    }
    const int bits = static_cast<int>(target_[Dither]);
    if (bits > 0) {   // TPDF dither + requantization
        const double q = std::ldexp(1.0, bits - 1);
        auto uni = [this] { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_ / 4294967296.0 - 0.5; };
        for (int c = 0; c < nch; ++c)
            for (int i = 0; i < n; ++i) ch[c][i] = static_cast<float>(std::round(ch[c][i] * q + uni() + uni()) / q);
    }
}

}  // namespace sw::ms01
