#include "vo06/vo06.hpp"
#include <algorithm>
#include <cmath>

namespace sw::vo06 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"vo06.pitch",      "Pitch",       -12, 12, 0,  Curve::Lin, 1, {}, "st"},
        {"vo06.formant",    "Formant",     -5, 5, 0,    Curve::Lin, 1, {}, ""},
        {"vo06.character",  "Character",   0, 3, 0,     Curve::Step, 1, {0, 1, 2, 3}, "", {"Neutral", "Deep", "Bright", "Child"}},
        {"vo06.keeptiming", "Keep timing", 0, 1, 1,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"vo06.smooth",     "Smooth",      0, 1, 0,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"vo06.mix",        "Mix",         0, 100, 100, Curve::Lin, 1, {}, "%"},
    };
    return s;
}
PitchConfig engineConfig(double fs) { PitchConfig c; c.fs = fs; c.minF0 = 85.0; c.maxF0 = 1000.0; c.windowPeriods = 2.0; return c; }
std::array<double, 2> characterSemis(int c) {
    switch (c) { case Deep: return {-2.0, -3.0}; case Bright: return {0.0, 2.0}; case Child: return {4.0, 4.0}; default: return {0.0, 0.0}; }
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

int Processor::latencySamples() const { return PitchAnalyzer::latencyFor(engineConfig(fs_)); }

void Processor::wanted(double& p, double& f) const {
    const auto c = characterSemis(static_cast<int>(target_[Character] + 0.5));
    p = target_[Pitch] + c[0];
    f = target_[Formant] + c[1];
    if (target_[KeepTiming] < 0.5) f += p;   // the formant follows the pitch
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    an_.prepare(engineConfig(fs_)); synth_.prepare(an_);
    wanted(curPitch_, curFormant_);
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::snapToTargets() { if (prepared_) wanted(curPitch_, curFormant_); }

void Processor::ratio(double, bool, double dt, double& pitchRatio, double& formantRatio) {
    double p, f; wanted(p, f);
    if (target_[Smooth] > 0.5) { const double k = 1.0 - std::exp(-dt / 0.04); curPitch_ += k * (p - curPitch_); curFormant_ += k * (f - curFormant_); }
    else { curPitch_ = p; curFormant_ = f; }
    pitchRatio = std::exp2(curPitch_ / 12.0); formantRatio = std::exp2(curFormant_ / 12.0);
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    for (int i = 0; i < n; ++i) {
        const double x = nch > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i];
        an_.push(x);
        double y = synth_.process(an_, *this);
        if (std::abs(y) < 1e-30) y = 0.0;
        for (int c = 0; c < nch; ++c) ch[c][i] = static_cast<float>(y);
    }
}

}  // namespace sw::vo06
