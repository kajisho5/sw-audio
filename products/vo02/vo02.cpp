#include "vo02/vo02.hpp"
#include <algorithm>
#include <cmath>

namespace sw::vo02 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"vo02.key",      "Key",      0, 11, 0,  Curve::Step, 1, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, "", {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}},
        {"vo02.scale",    "Scale",    0, 2, 0,   Curve::Step, 1, {0, 1, 2}, "", {"Maj", "Min", "Chr"}},
        {"vo02.speed",    "Speed",    0, 100, 25, Curve::Skew, 2, {}, "ms", {}, nullptr, nullptr, 1.0, true, true},
        {"vo02.humanize", "Humanize", 0, 10, 3,  Curve::Lin, 1, {}, ""},
        {"vo02.formant",  "Formant",  -3, 3, 0,  Curve::Lin, 1, {}, "st"},
        {"vo02.mix",      "Mix",      0, 100, 100, Curve::Lin, 1, {}, "%"},
    };
    return s;
}
PitchConfig engineConfig(double fs) { PitchConfig c; c.fs = fs; c.minF0 = 110.0; c.maxF0 = 1000.0; c.windowPeriods = 1.5; return c; }

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

int Processor::latencySamples() const { return PitchAnalyzer::latencyFor(engineConfig(fs_)); }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    an_.prepare(engineConfig(fs_)); synth_.prepare(an_); corr_.reset();
    apply();
    prepared_ = true;
}

void Processor::apply() {
    PitchCorrector::Settings s;
    s.mask = scaleBits(static_cast<int>(target_[Scale] + 0.5), static_cast<int>(target_[Key] + 0.5));
    s.speedMs = target_[Speed]; s.humanize = target_[Humanize] * 0.1; s.vibrato = 1.0;
    s.formantSemis = target_[Formant]; s.formantFollow = false; s.transpose = 0.0; s.weakenWhenUnstable = true; s.enabled = true;
    corr_.setSettings(s);
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (prepared_) apply();
}

void Processor::snapToTargets() { if (prepared_) apply(); }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    for (int i = 0; i < n; ++i) {
        const double x = nch > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i];
        an_.push(x);
        double y = synth_.process(an_, corr_);
        if (std::abs(y) < 1e-30) y = 0.0;
        for (int c = 0; c < nch; ++c) ch[c][i] = static_cast<float>(y);
    }
}

}  // namespace sw::vo02
