#include "dy04/dy04.hpp"
#include <algorithm>
#include <cmath>

namespace sw::dy04 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
        {"dy04.thresh",   "Threshold", -80, 0, -80,      Curve::Lin,  1, {}, "dBFS"},
        {"dy04.range",     "Range",     -80, 0, -40,      Curve::Lin,  1, {}, "dB"},
        {"dy04.attack",    "Attack",    0.01, 25, 0.1,    Curve::Log,  1, {}, "ms"},
        {"dy04.hold",      "Hold",      0, 2000, 50,      Curve::Skew, 3, {}, "ms"},
        {"dy04.release",   "Release",   5, 4000, 100,     Curve::Log,  1, {}, "ms"},
        {"dy04.mode",      "Mode",      0, 2, 0,          Curve::Step, 1, {0, 1, 2}, "", {"Gate", "Expand", "Duck"}},
        {"dy04.key.hpf",    "Key HPF",   0, 1, 0,          Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"dy04.key.lpf",    "Key LPF",   0, 1, 0,          Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"dy04.key.hpffreq", "Key HPF Hz", 20, 2000, 100,   Curve::Log,  1, {}, "Hz"},
        {"dy04.key.lpffreq", "Key LPF Hz", 1000, 20000, 8000, Curve::Log, 1, {}, "Hz"},
        {"dy04.listen",    "Listen",    0, 1, 0,          Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        };
        v[Listen].automatable = false;  // monitoring (spec: Auto —)
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    gate_.prepare(fs_);
    for (auto& f : hp_) f.reset();
    for (auto& f : lp_) f.reset();
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
}

void Processor::applyGate() {
    gate_.set(static_cast<GateEngine::Mode>(static_cast<int>(target_[Mode])), target_[Threshold], target_[Range],
              target_[Attack], target_[Hold], target_[Release]);
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case Threshold: case Range: case Attack: case Hold: case Release: case Mode: applyGate(); break;
        case KeyHpfHz: for (auto& f : hp_) f.setup(Svf::Mode::HighPass, v, fs_, 0.70710678, 0); break;
        case KeyLpfHz: for (auto& f : lp_) f.setup(Svf::Mode::LowPass, v, fs_, 0.70710678, 0); break;
        default: break;
    }
}

void Processor::run(float** ch, int numCh, int n, const float* const* sc, int scCh) {
    const int nch = std::min(numCh, 2);
    const bool ext = sc != nullptr && scCh > 0, hpOn = target_[KeyHpf] > 0.5, lpOn = target_[KeyLpf] > 0.5, listen = target_[Listen] > 0.5;
    for (int i = 0; i < n; ++i) {
        double key[2] = {0, 0}, level = 0;
        for (int c = 0; c < nch; ++c) {
            double k = ext ? sc[std::min(c, scCh - 1)][i] : ch[c][i];
            if (hpOn) k = hp_[static_cast<size_t>(c)].process(k);
            if (lpOn) k = lp_[static_cast<size_t>(c)].process(k);
            key[c] = k;
            level = std::max(level, std::abs(k));
        }
        const double g = gate_.process(level);
        for (int c = 0; c < nch; ++c) {
            double y = listen ? key[c] : ch[c][i] * g;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::dy04
