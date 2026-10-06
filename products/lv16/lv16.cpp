#include "lv16/lv16.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv16 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv16.mode",      "Mode",      0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Gate", "Duck"}},
        {"lv16.threshold", "Threshold", -80, 0, -42,  Curve::Lin,  1, {}, "dBFS"},
        {"lv16.range",     "Range",     -80, 0, -40,  Curve::Lin,  1, {}, "dB"},
        {"lv16.hold",      "Hold",      0, 2000, 80,  Curve::Skew, 3, {}, "ms"},
        {"lv16.release",   "Release",   5, 4000, 250, Curve::Log,  1, {}, "ms"},
        {"lv16.evo.keyhpf", "Key HPF",  0, 1, 1,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

namespace { constexpr double kAttackMs = 0.5, kKeyHz = 120.0; }

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    gate_.prepare(fs_);
    const double q[2] = {0.5411961, 1.3065630};  // 4th-order Butterworth
    for (auto& c : hp_) for (int k = 0; k < 2; ++k) { c[static_cast<size_t>(k)].reset(); c[static_cast<size_t>(k)].setup(Svf::Mode::HighPass, kKeyHz, fs_, q[k], 0); }
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
}

void Processor::applyGate() {
    const auto m = target_[Mode] > 0.5 ? GateEngine::Mode::Duck : GateEngine::Mode::Gate;
    gate_.set(m, target_[Threshold], target_[Range], kAttackMs, target_[Hold], target_[Release]);
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v));
    if (id != KeyHpf) applyGate();
}

void Processor::run(float** ch, int numCh, int n, const float* const* sc, int scCh) {
    const int nch = std::min(numCh, 2);
    const bool ext = sc != nullptr && scCh > 0, hpOn = target_[KeyHpf] > 0.5;
    for (int i = 0; i < n; ++i) {
        double level = 0;
        for (int c = 0; c < nch; ++c) {
            double k = ext ? sc[std::min(c, scCh - 1)][i] : ch[c][i];
            if (hpOn) for (auto& f : hp_[static_cast<size_t>(c)]) k = f.process(k);
            level = std::max(level, std::abs(k));
        }
        const double g = gate_.process(level);
        for (int c = 0; c < nch; ++c) {
            double y = ch[c][i] * g;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::lv16
