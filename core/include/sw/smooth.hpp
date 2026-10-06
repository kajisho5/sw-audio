// SW AUDIO core — linear ramp smoother (spec: ゲイン類は 20 ms のランプ)
#pragma once
#include <algorithm>
#include <cmath>

namespace sw {

class LinearSmoother {
public:
    void reset(double sampleRate, double rampMs, double value) {
        rampSamples_ = std::max(1, static_cast<int>(std::lround(sampleRate * rampMs * 0.001)));
        current_ = target_ = value;
        remaining_ = 0;
    }
    void setTarget(double v) {
        if (v == target_) return;
        target_ = v;
        remaining_ = rampSamples_;
        step_ = (target_ - current_) / rampSamples_;
    }
    double next() {
        if (remaining_ > 0) {
            --remaining_;
            current_ = remaining_ == 0 ? target_ : current_ + step_;
        }
        return current_;
    }
    // advance n samples at once (control-rate updates)
    double skip(int n) {
        if (remaining_ <= 0 || n <= 0) return current_;
        if (n >= remaining_) { remaining_ = 0; current_ = target_; return current_; }
        remaining_ -= n;
        current_ += step_ * n;
        return current_;
    }
    bool isSmoothing() const { return remaining_ > 0; }
    double current() const { return current_; }
    double target() const { return target_; }

private:
    int rampSamples_ = 1, remaining_ = 0;
    double current_ = 0, target_ = 0, step_ = 0;
};

}  // namespace sw
