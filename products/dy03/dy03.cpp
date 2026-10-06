#include "dy03/dy03.hpp"
#include <algorithm>
#include <cmath>

namespace sw::dy03 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"dy03.thresh", "Threshold", -30, 0, 0,  Curve::Lin,  1, {}, "dB"},
            {"dy03.ratio",     "Ratio",     1.5, 10, 2, Curve::Step, 1, {1.5, 2, 4, 10}, ":1", {"1.5:1", "2:1", "4:1", "10:1"}},
            {"dy03.attack",    "Attack",    0.1, 100, 10, Curve::Step, 1, {0.1, 0.3, 1, 3, 10, 30, 100}, "ms", {"0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms", "100 ms"}},
            {"dy03.release",   "Release",   50, 10000, 10000, Curve::Step, 1, {50, 100, 200, 400, 800, 10000}, "ms", {"50 ms", "100 ms", "200 ms", "400 ms", "800 ms", "Auto"}},
            {"dy03.makeup",    "Makeup",    0, 20, 0,   Curve::Lin,  1, {}, "dB"},
            {"dy03.mix",       "Mix",       0, 100, 100, Curve::Lin, 1, {}, "%"},
            {"dy03.knee",      "Knee",      0, 12, 0,   Curve::Lin,  1, {}, "dB"},
            {"dy03.schpf",     "SC HPF",    20, 300, 20, Curve::Log, 1, {}, "Hz"},
            {"dy03.evo.on",    "Punch keep", 0, 1, 0,   Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        };
        v[ScHpf].minLabel = "Off";
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& d : det_) d.set(fs_, LevelDetector::Mode::Program);
    low_.setup(Svf::Mode::LowPass, 150.0, fs_, 0.70710678, 0);
    midHp_.setup(Svf::Mode::HighPass, 1000.0, fs_, 0.70710678, 0);
    midLp_.setup(Svf::Mode::LowPass, 5000.0, fs_, 0.70710678, 0);
    envRel_ = Ballistics::coef(fs_, 20.0);
    envSlow_ = Ballistics::coef(fs_, 30.0);
    punchCoef_ = Ballistics::coef(fs_, 2.0);
    fast_.reset(0.0); slow_.reset(0.0);
    gr_ = 0; punch_ = 1; hold_ = 0; fastEnv_ = slowEnv_ = 0;
    makeup_.reset(fs_, 20.0, std::pow(10.0, target_[Makeup] / 20.0));
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case Threshold: case Ratio: case Attack: case Release: case Knee: updateDynamics(); break;
        case Makeup: makeup_.setTarget(std::pow(10.0, v / 20.0)); break;
        case ScHpf:
            hpfOn_ = v > sp.min * 1.0001;
            for (auto& h : hpf_) h.setup(Svf::Mode::HighPass, v, fs_, 0.70710678, 0);
            break;
        default: break;
    }
}

void Processor::updateDynamics() {
    comp_.set(target_[Threshold], target_[Ratio], target_[Knee]);
    if (autoRelease()) {
        fast_.set(fs_, target_[Attack], 100.0);   // short peaks recover in about 100 ms
        slow_.set(fs_, 1000.0, 1200.0);           // only sustained compression builds the slow stage
    } else {
        fast_.set(fs_, target_[Attack], target_[Release]);
    }
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const bool punch = target_[PunchKeep] > 0.5, autoRel = autoRelease();
    for (int i = 0; i < n; ++i) {
        double level = 0, mono = 0;
        for (int c = 0; c < nch; ++c) {
            const double x = ch[c][i];
            mono += x;
            const double key = hpfOn_ ? hpf_[static_cast<size_t>(c)].process(x) : x;
            level = std::max(level, det_[static_cast<size_t>(c)].process(key));
        }
        const double target = comp_.gainDb(20.0 * std::log10(std::max(level, 1e-9)));
        double gr = fast_.process(target);
        if (autoRel) gr = std::min(gr, slow_.process(target));
        if (punch) {
            // onset = fast envelope 6 dB above the slow one, in the low or mid band
            const double band = std::max(std::abs(low_.process(mono)), std::abs(midLp_.process(midHp_.process(mono))));
            fastEnv_ = std::max(band, envRel_ * fastEnv_);
            slowEnv_ = band + envSlow_ * (slowEnv_ - band);
            if (fastEnv_ > 2.0 * slowEnv_ && fastEnv_ > 1e-4) hold_ = static_cast<int>(0.015 * fs_);
            const double want = hold_ > 0 ? 0.3 : 1.0;
            if (hold_ > 0) --hold_;
            punch_ = want + punchCoef_ * (punch_ - want);
            gr *= punch_;
        }
        gr_ = gr;
        const double g = std::pow(10.0, gr / 20.0) * makeup_.next();
        for (int c = 0; c < nch; ++c) {
            double y = ch[c][i] * g;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::dy03
