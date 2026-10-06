#include "dy08/dy08.hpp"
#include <algorithm>
#include <cmath>

namespace sw::dy08 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"dy08.thresh",      "Threshold",    -60, 0, 0,       Curve::Lin,  1, {}, "dB"},
            {"dy08.ratio",       "Ratio",        1, 20, 2,        Curve::Log,  1, {}, ":1"},
            {"dy08.knee",        "Knee",         0, 24, 6,        Curve::Lin,  1, {}, "dB"},
            {"dy08.attack",      "Attack",       0.05, 200, 10,   Curve::Skew, 3, {}, "ms"},
            {"dy08.release",     "Release",      5, 3000, 150,    Curve::Skew, 3, {}, "ms"},
            {"dy08.evo.on"    , "Auto release", 0, 1, 0,         Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"dy08.makeup",      "Makeup",       -12, 24, 0,      Curve::Lin,  1, {}, "dB"},
            {"dy08.mix",         "Mix",          0, 100, 100,     Curve::Lin,  1, {}, "%"},
            {"dy08.schpf",       "SC HPF",       20, 300, 20,     Curve::Log,  1, {}, "Hz"},
            {"dy08.detector",    "Detector",     0, 2, 2,         Curve::Step, 1, {0, 1, 2}, "", {"Peak", "RMS", "Program"}},
            {"dy08.lookahead",   "Lookahead",    0, 1, 0,         Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"dy08.sc",          "Sidechain",    0, 1, 0,         Curve::Step, 1, {0, 1}, "", {"Internal", "External"}},
        };
        v[Ratio].maxLabel = "inf";   // spec: rightmost 5 % is infinity
        v[Ratio].maxLabelNorm = 0.95;
        v[ScHpf].minLabel = "Off";
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

int Processor::latencySamples() const {
    return target_[Lookahead] > 0.5 ? static_cast<int>(std::lround(fs_ * 0.005)) : 0;  // 5 ms
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    la_ = latencySamples();
    for (auto& d : delay_) d.assign(static_cast<size_t>(std::max(1, la_)), 0.0f);
    dpos_ = 0;
    for (auto& h : hpf_) h.reset();
    makeup_.reset(fs_, 20.0, 1.0);
    ball_.reset(0.0);
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
    snapToTargets();
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case Threshold: case Ratio: case Knee: case Attack: case Release: case AutoRelease: updateDynamics(); break;
        case Makeup: makeup_.setTarget(std::pow(10.0, v / 20.0)); break;
        case ScHpf:
            hpfOn_ = v > sp.min * 1.0001;
            for (auto& h : hpf_) h.setup(Svf::Mode::HighPass, v, fs_, 0.70710678, 0);
            break;
        case Detector:
            for (auto& d : det_) d.set(fs_, static_cast<LevelDetector::Mode>(static_cast<int>(v)));
            break;
        default: break;  // Mix: sw::Shell, Lookahead: next prepare
    }
}

void Processor::updateDynamics() {
    const auto& s = specs();
    const double ratio = isInfiniteRatio(s[Ratio], target_[Ratio]) ? GainComputer::kInfinity : target_[Ratio];
    comp_.set(target_[Threshold], ratio, target_[Knee]);
    ball_.set(fs_, target_[Attack], effectiveReleaseMs());
}

double Processor::effectiveReleaseMs() const {
    if (target_[AutoRelease] < 0.5) return target_[Release];
    if (tempo_ <= 0.0) return 150.0;
    return std::clamp(60000.0 / tempo_ / 4.0, 30.0, 1000.0);  // a 16th note: GR recovers before the next beat
}

void Processor::setTempo(double bpm) {
    if (bpm == tempo_) return;
    tempo_ = bpm;
    if (target_[AutoRelease] > 0.5) ball_.setRelease(fs_, effectiveReleaseMs());
}

void Processor::snapToTargets() { makeup_.skip(1 << 30); }

void Processor::run(float** ch, int numCh, int n, const float* const* sc, int scCh) {
    const int nch = std::min(numCh, 2);
    const bool external = target_[Sidechain] > 0.5 && sc != nullptr && scCh > 0;
    for (int i = 0; i < n; ++i) {
        double level = 0;
        for (int c = 0; c < nch; ++c) {
            const double x = external ? sc[std::min(c, scCh - 1)][i] : ch[c][i];
            const double key = hpfOn_ ? hpf_[static_cast<size_t>(c)].process(x) : x;
            level = std::max(level, det_[static_cast<size_t>(c)].process(key));
        }
        const double levelDb = 20.0 * std::log10(std::max(level, 1e-9));
        grDb_ = ball_.process(comp_.gainDb(levelDb));
        const double g = std::pow(10.0, grDb_ / 20.0) * makeup_.next();
        for (int c = 0; c < nch; ++c) {
            double x = ch[c][i];
            if (la_ > 0) {
                auto& d = delay_[static_cast<size_t>(c)];
                const float xd = d[static_cast<size_t>(dpos_)];
                d[static_cast<size_t>(dpos_)] = static_cast<float>(x);
                x = xd;
            }
            double y = x * g;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
        if (la_ > 0) dpos_ = (dpos_ + 1) % la_;
    }
}

}  // namespace sw::dy08
