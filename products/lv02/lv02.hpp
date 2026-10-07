// SW LV02 Feedback — howling suppressor (spec: 仕様書 v1.0「LV02 Feedback」). Reported delay 0. Detection and filters: sw/feedback.hpp (12 slots F1..F12, FIXED and LIVE).
//   Buttons (methods, not parameters; the spec says "Auto 不可"): ringOut(on) / lockFilters() / clearLive().
//   The FIXED filters are part of the saved state (saveExtra / loadExtra); LIVE filters are not.
#pragma once
#include "sw/feedback.hpp"
#include "sw/param.hpp"
#include <vector>

namespace sw::lv02 {

enum ParamId { Sensitivity, MaxDepth, Width, Release, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n) { if (prepared_ && numCh > 0 && n > 0) guard_.process(ch, numCh, n); }
    int latencySamples() const { return 0; }
    void ringOut(bool on) { guard_.ringOut(on); }
    void lockFilters() { guard_.lockFilters(); }
    void clearLive() { guard_.clearLive(); }
    const FeedbackGuard& guard() const { return guard_; }
    void addFixed(double freq, double depthDb) { guard_.addFixed(freq, depthDb); }
    void saveExtra(std::vector<uint8_t>& out) const;
    void loadExtra(const uint8_t* data, size_t size);

private:
    void apply() { guard_.setParams(static_cast<int>(target_[Sensitivity]), target_[MaxDepth], target_[Width], target_[Release]); }
    FeedbackGuard guard_;
    std::array<double, kNumParams> target_{};
    bool prepared_ = false;
    std::vector<std::pair<double, double>> pending_;
};

}  // namespace sw::lv02
