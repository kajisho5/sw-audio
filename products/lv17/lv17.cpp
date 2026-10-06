#include "lv17/lv17.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv17 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"lv17.mode",      "Mode",      0, 2, 0,        Curve::Step, 1, {0, 1, 2}, "", {"Speech", "Music", "Band"}},
            {"lv17.threshold", "Threshold", -40, 0, -20,    Curve::Lin,  1, {}, "dB"},
            {"lv17.ratio",     "Ratio",     1, 10, 3,       Curve::Log,  1, {}, ":1"},
            {"lv17.attack",    "Attack",    0.1, 100, 10,   Curve::Skew, 3, {}, "ms"},
            {"lv17.release",   "Release",   10, 2000, 2000, Curve::Skew, 3, {}, "ms"},
            {"lv17.makeup",    "Makeup",    0, 20, 3,       Curve::Lin,  1, {}, "dB"},
        };
        v[Release].maxLabel = "Auto";
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    fast_.reset(0); slow_.reset(0);
    makeup_.reset(fs_, 20.0, std::pow(10.0, target_[Makeup] / 20.0));
    update();
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Makeup) makeup_.setTarget(std::pow(10.0, v / 20.0));
    else update();
}

void Processor::update() {
    static const LevelDetector::Mode det[3] = {LevelDetector::Mode::Rms, LevelDetector::Mode::Program, LevelDetector::Mode::Peak};
    static const double knee[3] = {6.0, 4.0, 2.0};
    const int m = static_cast<int>(target_[Mode]);
    for (auto& d : det_) d.set(fs_, det[m]);
    comp_.set(target_[Threshold], target_[Ratio], knee[m]);
    if (autoRelease()) { fast_.set(fs_, target_[Attack], 100.0); slow_.set(fs_, 1000.0, 1200.0); }
    else fast_.set(fs_, target_[Attack], target_[Release]);
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const bool autoRel = autoRelease();
    for (int i = 0; i < n; ++i) {
        double level = 0;
        for (int c = 0; c < nch; ++c) level = std::max(level, det_[static_cast<size_t>(c)].process(ch[c][i]));
        const double target = comp_.gainDb(20.0 * std::log10(std::max(level, 1e-9)));
        double gr = fast_.process(target);
        if (autoRel) gr = std::min(gr, slow_.process(target));
        const double g = std::pow(10.0, gr / 20.0) * makeup_.next();
        for (int c = 0; c < nch; ++c) {
            double y = ch[c][i] * g;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::lv17
