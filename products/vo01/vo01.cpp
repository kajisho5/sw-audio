#include "vo01/vo01.hpp"
#include <algorithm>
#include <cmath>

namespace sw::vo01 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"vo01.view",       "View",       0, 1, 1,   Curve::Step, 1, {0, 1}, "", {"Graph", "Auto"}},
        {"vo01.scale",      "Scale",      0, 2, 0,   Curve::Step, 1, {0, 1, 2}, "", {"Major", "Chromatic", "Custom"}},
        {"vo01.speed",      "Speed",      0, 400, 20, Curve::Skew, 2, {}, "ms"},
        {"vo01.humanize",   "Humanize",   0, 100, 40, Curve::Lin, 1, {}, "%"},
        {"vo01.vibrato",    "Vibrato",    0, 2, 0,   Curve::Step, 1, {0, 1, 2}, "", {"Natural", "Reduce", "Flat"}},
        {"vo01.formant",    "Formant",    0, 1, 0,   Curve::Step, 1, {0, 1}, "", {"Keep", "Follow"}},
        {"vo01.transpose",  "Transpose",  -12, 12, 0, Curve::Step, 1, {-12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, "st"},
        {"vo01.detectmidi", "Detect MIDI", 0, 1, 0,  Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"vo01.snap",       "Snap to grid", 0, 1, 1, Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"vo01.reference",  "Reference",  0, 1, 0,   Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"vo01.key",        "Key",        0, 11, 0,  Curve::Step, 1, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, "", {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}},
        {"vo01.customscale", "Custom scale", 0, 4095, 2741, Curve::Lin, 1, {}, "", {}, nullptr, nullptr, 1.0, false},
    };
    return s;
}
PitchConfig engineConfig(double fs) { PitchConfig c; c.fs = fs; c.minF0 = 85.0; c.maxF0 = 1000.0; c.windowPeriods = 2.0; return c; }

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
    const int scale = static_cast<int>(target_[Scale] + 0.5), key = static_cast<int>(target_[Key] + 0.5);
    s.mask = scale == Custom ? static_cast<uint16_t>(static_cast<int>(target_[CustomScale]) & 0x0FFF) : scaleBits(scale == Chromatic ? 2 : 0, key);
    s.speedMs = target_[Speed]; s.humanize = target_[Humanize] * 0.01;
    const int vib = static_cast<int>(target_[Vibrato] + 0.5); s.vibrato = vib == Natural ? 1.0 : vib == Reduce ? 0.4 : 0.0;
    s.formantFollow = target_[Formant] > 0.5; s.transpose = std::round(target_[Transpose]);
    s.enabled = target_[View] > 0.5;
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

}  // namespace sw::vo01
