#include "lv10/lv10.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv10 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv10.preset",  "Preset",  0, 5, 0,    Curve::Step, 1, {0, 1, 2, 3, 4, 5}, "", {"None", "Low", "High", "Robot", "Radio", "Anon"}},
        {"lv10.pitch",   "Pitch",   -12, 12, -3, Curve::Lin, 1, {}, "st"},
        {"lv10.formant", "Formant", -5, 5, 2,   Curve::Lin, 1, {}, "st"},
        {"lv10.robot",   "Robot",   0, 1, 0,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"lv10.mix",     "Mix",     0, 100, 100, Curve::Lin, 1, {}, "%"},
        {"lv10.monitor", "Monitor", 0, 1, 1,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

PitchConfig engineConfig(double fs) { PitchConfig c; c.fs = fs; c.minF0 = 110.0; c.maxF0 = 1000.0; c.windowPeriods = 1.5; return c; }

std::vector<std::pair<int, double>> presetValues(int p) {
    switch (p) {
        case Low: return {{Pitch, -5}, {Formant, -2}, {Robot, 0}};
        case High: return {{Pitch, 5}, {Formant, 2}, {Robot, 0}};
        case RobotP: return {{Pitch, 0}, {Formant, 0}, {Robot, 1}};
        case Radio: return {{Pitch, 0}, {Formant, 0}, {Robot, 0}};
        case Anon: return {{Pitch, -3}, {Formant, 2}, {Robot, 0}};
        default: return {};
    }
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

int Processor::latencySamples() const { return PitchAnalyzer::latencyFor(engineConfig(fs_)); }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate; an_.prepare(engineConfig(fs_)); synth_.prepare(an_);
    hp_.setup(Svf::Mode::HighPass, 300.0, fs_, 0.70710678, 0); lp_.setup(Svf::Mode::LowPass, std::min(3400.0, fs_ * 0.45), fs_, 0.70710678, 0);
    apz_.fill(0.0); jitter_ = jitterTarget_ = jitterClock_ = 0; writes_.clear(); prepared_ = true;
}

void Processor::applyParam(int id, double v) { target_[static_cast<size_t>(id)] = v; }
void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    applyParam(id, v);
    if (id != Preset) writes_.erase(std::remove_if(writes_.begin(), writes_.end(), [id](const std::pair<int, double>& w) { return w.first == id; }), writes_.end());
    else { const auto p = presetValues(static_cast<int>(v + 0.5)); for (const auto& w : p) applyParam(w.first, w.second); writes_ = p; }
}
int Processor::takeParamWrite(int& id, double& plain) {
    if (writes_.empty()) return 0;
    id = writes_.front().first; plain = writes_.front().second; writes_.erase(writes_.begin());
    return 7;
}

void Processor::ratio(double f0, bool voiced, double dt, double&r, double& fm) {
    r = 1.0; fm = 1.0;
    const bool anon = static_cast<int>(target_[Preset] + 0.5) == Anon;
    if (anon) {   // formant drift: a smoothed random walk, a new target about every 0.7 s
        jitterClock_ -= dt;
        if (jitterClock_ <= 0.0) { jitterClock_ = 1.0 / 0.7; jitterTarget_ = (static_cast<double>(rng_() % 2001) / 1000.0 - 1.0) * 0.7; }
        jitter_ += (jitterTarget_ - jitter_) * (1.0 - std::exp(-dt / 0.3));
    } else jitter_ = 0.0;
    const double pr = std::exp2(target_[Pitch] / 12.0);
    if (voiced && f0 > 0.0) {
        r = pr;
        if (target_[Robot] > 0.5) r = std::clamp(120.0 * pr / f0, 0.5, 2.0);
    }
    fm = std::exp2((target_[Formant] + jitter_) / 12.0);
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    const int nch = std::min(numCh, 2);
    if (target_[Monitor] < 0.5) return;   // untouched
    const int preset = static_cast<int>(target_[Preset] + 0.5);
    static constexpr double kAp[6] = {0.55, -0.45, 0.62, -0.38, 0.5, -0.57};
    for (int i = 0; i < n; ++i) {
        const double x = nch > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i];
        an_.push(x);
        double y = synth_.process(an_, *this);
        if (preset == Radio) { y = lp_.process(hp_.process(y)); y = std::tanh(1.8 * y) / 1.8; }
        if (preset == Anon) for (int k = 0; k < 6; ++k) { const double a = kAp[k], v = y - a * apz_[static_cast<size_t>(k)]; const double o = a * v + apz_[static_cast<size_t>(k)]; apz_[static_cast<size_t>(k)] = v; y = o; }
        if (!std::isfinite(y) || std::abs(y) < 1e-30) y = 0.0;
        for (int c = 0; c < nch; ++c) ch[c][i] = static_cast<float>(y);
    }
}

}  // namespace sw::lv10
