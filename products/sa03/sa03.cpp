#include "sa03/sa03.hpp"
#include <algorithm>
#include <cmath>

namespace sw::sa03 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"sa03.drive",  "Drive",  0, 10, 3,     Curve::Lin,  1, {}, ""},
            {"sa03.bias",   "Bias",   0, 1, 0.5,    Curve::Lin,  1, {}, ""},
            {"sa03.tone",   "Tone",   -6, 6, 0,     Curve::Lin,  1, {}, "dB"},
            {"sa03.tube",   "Tube",   0, 2, 0,      Curve::Step, 1, {0, 1, 2}, "", {"12AX7", "12AT7", "EL34"}},
            {"sa03.mix",    "Mix",    0, 100, 100,  Curve::Lin,  1, {}, "%"},
            {"sa03.output", "Output", -10, 10, 0,   Curve::Lin,  1, {}, "dB"},
            {"sa03.evo.on", "Moving bias", 0, 1, 1, Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            oversampleSpec("sa03.os", 4.0),   // the spec: 2x OS, 4x recommended
            unitSpec("sa03.unit"),
        };
        v[Bias].minLabel = "Cold"; v[Bias].maxLabel = "Hot";
        return v;
    }();
    return s;
}

namespace {
// per tube: drive scale (the dB per Drive step), centre bias, headroom (design values)
struct TubeModel { double driveScale, bias, headroom; };
constexpr TubeModel kTube[3] = {{1.0, 0.30, 2.0}, {0.8, 0.18, 2.0}, {0.7, 0.08, 1.4}};   // 12AX7 / 12AT7 / EL34
constexpr double kDbPerDrive = 2.4, kShiftMax = 0.35;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    shaper_.prepare(fs_); shaper_.setOversample(static_cast<int>(target_[Oversample]));
    applyUnit();
    envC_ = std::exp(-1.0 / (0.050 * fs_));
    env_ = shift_ = 0;
    updateTone();
}

void Processor::applyUnit() {
    unit_ = static_cast<int>(target_[Unit]);
    for (int c = 0; c < 2; ++c) shaper_.setOnsetDb(c, sw::Unit::satDb(unit_, c, 0));   // where the tube saturates, per channel
}

void Processor::updateTone() {
    const double t = target_[Tone];
    for (int k = 0; k < 2; ++k) {   // each channel's tone network has its own tolerance (Unit B / C)
        const double f = 1000.0 * sw::Unit::freqMul(unit_, k, 0);
        tone_[static_cast<size_t>(k)].lo.setup(Svf::Mode::LowShelf, f, fs_, 0.5, -t); tone_[static_cast<size_t>(k)].hi.setup(Svf::Mode::HighShelf, f, fs_, 0.5, t);
    }
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Tone) updateTone();
    else if (id == Oversample) shaper_.setOversample(static_cast<int>(v));
    else if (id == Unit) { applyUnit(); updateTone(); }
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const TubeModel& m = kTube[static_cast<int>(target_[Tube] + 0.5)];
    const double g = std::pow(10.0, target_[Drive] * kDbPerDrive * m.driveScale / 20.0);
    const double bias0 = std::clamp(target_[Bias] * 2.0 * m.bias, 0.0, 0.9);   // Cold 0 (symmetric) .. centre = the tube's bias .. Hot twice that
    const bool moving = target_[Evo] > 0.5, tone = std::abs(target_[Tone]) > 1e-9;
    for (int i = 0; i < n; ++i) {
        double peak = 0;
        for (int c = 0; c < nch; ++c) peak = std::max(peak, std::abs(static_cast<double>(ch[c][i])));
        env_ = std::max(peak, envC_ * env_);
        shift_ = moving ? kShiftMax * std::tanh(4.0 * env_) : 0.0;
        const double b = std::min(bias0 + shift_, 0.9);
        for (int c = 0; c < nch; ++c) {
            double y = shaper_.process(c, ch[c][i], g, b, m.headroom);
            if (tone) y = tone_[static_cast<size_t>(c)].hi.process(tone_[static_cast<size_t>(c)].lo.process(y));
            if (!std::isfinite(y)) y = 0.0;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::sa03
