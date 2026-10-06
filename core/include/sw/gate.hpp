// SW AUDIO core — gate / expander / ducker engine (DY04, LV16, CS02 ...)
// Key level -> peak envelope (instant attack, 10 ms decay) -> open/close with 4 dB hysteresis and hold
// -> gain target in dB (0 or Range; Expand: 1:2 below threshold, floored at Range) -> attack / release in dB.
#pragma once
#include <algorithm>
#include <cmath>

namespace sw {

class GateEngine {
public:
    enum class Mode { Gate, Expand, Duck };
    static constexpr double kHysteresisDb = 4.0;

    void prepare(double fs) {
        fs_ = fs;
        env_ = 0; open_ = false; holdLeft_ = 0; gDb_ = 0; fresh_ = true;
        envDecay_ = coef(10.0);
        set(mode_, thr_, range_, attackMs_, holdMs_, releaseMs_);
    }
    void set(Mode m, double thresholdDb, double rangeDb, double attackMs, double holdMs, double releaseMs) {
        mode_ = m; thr_ = thresholdDb; range_ = std::min(0.0, rangeDb);
        attackMs_ = attackMs; holdMs_ = holdMs; releaseMs_ = releaseMs;
        a_ = coef(attackMs); r_ = coef(releaseMs);
        holdLen_ = static_cast<long>(std::lround(holdMs * 0.001 * fs_));
        if (fresh_) gDb_ = mode_ == Mode::Gate ? range_ : 0.0;  // before audio: a gate starts closed, others at unity
    }
    // keyLevel: linear key amplitude for this sample (linked across channels); returns the linear gain
    double process(double keyLevel) {
        fresh_ = false;
        env_ = std::max(std::abs(keyLevel), envDecay_ * env_);
        const double lv = 20.0 * std::log10(std::max(env_, 1e-9));
        // open / close with hysteresis and hold
        if (lv >= thr_) { open_ = true; holdLeft_ = holdLen_; }
        else if (open_ && lv >= thr_ - kHysteresisDb) holdLeft_ = holdLen_;
        else if (open_ && holdLeft_ > 0) --holdLeft_;
        else if (open_) open_ = false;
        double target = 0.0;
        switch (mode_) {
            case Mode::Gate: target = open_ ? 0.0 : range_; break;
            case Mode::Expand: target = lv < thr_ ? std::max(range_, lv - thr_) : 0.0; break;
            case Mode::Duck: target = open_ ? range_ : 0.0; break;
        }
        // Gate/Expand: rising gain = attack. Duck: falling gain = attack.
        const bool attack = mode_ == Mode::Duck ? target < gDb_ : target > gDb_;
        const double c = attack ? a_ : r_;
        gDb_ = target + c * (gDb_ - target);
        if (gDb_ != lastDb_) { lastDb_ = gDb_; lastGain_ = std::pow(10.0, gDb_ / 20.0); }  // settled: no pow per sample
        return lastGain_;
    }
    bool isOpen() const { return open_; }
    double gainDb() const { return gDb_; }

private:
    double coef(double ms) const { return ms <= 0.0 ? 0.0 : std::exp(-1.0 / (ms * 0.001 * fs_)); }
    Mode mode_ = Mode::Gate;
    double fs_ = 48000, thr_ = -40, range_ = -40, attackMs_ = 0.1, holdMs_ = 50, releaseMs_ = 100;
    double a_ = 0, r_ = 0, env_ = 0, envDecay_ = 0, gDb_ = 0, lastDb_ = 0, lastGain_ = 1;
    long holdLen_ = 0, holdLeft_ = 0;
    bool open_ = false, fresh_ = true;
};

}  // namespace sw
