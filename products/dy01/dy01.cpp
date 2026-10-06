#include "dy01/dy01.hpp"
#include <algorithm>
#include <cmath>

namespace sw::dy01 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"dy01.drive", "Drive",  0, 10, 0,   Curve::Lin, 1, {}, ""},
            {"dy01.ratio", "Ratio",  2, 20, 4,   Curve::Log, 1, {}, ":1"},
            {"dy01.speed", "Speed",  0, 1, 0.5,  Curve::Lin, 1, {}, ""},
            {"dy01.bite",  "Bite",   0, 100, 0,  Curve::Lin, 1, {}, "%"},
            {"dy01.color", "Color",  0, 2, 1,    Curve::Step, 1, {0, 1, 2}, "", {"Clean", "Grit", "Crush"}},
            {"dy01.out",   "Output", -12, 24, 0, Curve::Lin, 1, {}, "dB"},
            {"dy01.mix",   "Mix",    0, 100, 100, Curve::Lin, 1, {}, "%"},
            {"dy01.schpf", "SC HPF", 20, 300, 20, Curve::Log, 1, {}, "Hz"},
        };
        v[Ratio].maxLabel = "Max"; v[Ratio].maxLabelNorm = 0.95;  // spec: rightmost 5 % is Max
        v[Speed].minLabel = "Slow"; v[Speed].maxLabel = "Fast";
        v[SchPf].minLabel = "Off";
        return v;
    }();
    return s;
}

double attackMs(double speed) { return 0.8 * std::pow(0.02 / 0.8, std::clamp(speed, 0.0, 1.0)); }
double releaseMs(double speed) { return 1100.0 * std::pow(50.0 / 1100.0, std::clamp(speed, 0.0, 1.0)); }

namespace { constexpr double kThresholdDbfs = -6.0, kDcHz = 5.0; }

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    envRel_ = Ballistics::coef(fs_, 5.0);
    envSlow_ = Ballistics::coef(fs_, 30.0);
    relaxCoef_ = Ballistics::coef(fs_, 2.0);
    dcA2_ = std::exp(-2.0 * 3.14159265358979323846 * kDcHz / (2.0 * fs_));
    dcA4_ = std::exp(-2.0 * 3.14159265358979323846 * kDcHz / (4.0 * fs_));
    color_.reset();
    gr_ = 0; relax_ = 1.0; fastEnv_ = slowEnv_ = 0; hold_ = 0; onset_ = false;
    drive_.reset(fs_, 20.0, target_[Drive] * 3.6);
    gin_ = std::pow(10.0, drive_.current() / 20.0);
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case Drive: drive_.setTarget(v * 3.6); break;
        case Ratio: updateCurve(); break;
        case Speed: ball_.set(fs_, attackMs(v), releaseMs(v)); break;
        case SchPf:
            hpfOn_ = v > sp.min * 1.0001;
            for (auto& h : hpf_) h.setup(Svf::Mode::HighPass, v, fs_, 0.70710678, 0);
            break;
        default: break;
    }
}

void Processor::updateCurve() {
    // Max: ratio infinite and a hard knee (a soft 6 dB knee for the finite ratios)
    max_ = isInfiniteRatio(specs()[Ratio], target_[Ratio]);
    comp_.set(kThresholdDbfs, max_ ? GainComputer::kInfinity : target_[Ratio], max_ ? 0.0 : 6.0);
}

// asymmetric soft clip with unity small-signal gain: (tanh(g u + b) - tanh b) / (g sech^2 b); the DC shift is removed
double Processor::shape(double u, double g, double b, double& dc, double dcA) const {
    const double tb = std::tanh(b), s = 1.0 / (1.0 - tb * tb);
    const double y = s * (std::tanh(g * u + b) - tb) / g;
    const double d = y - u;
    dc = dcA * dc + (1.0 - dcA) * d;
    return u + (d - dc);
}

// design values (README "DY01 の設計"): Clean g 0.5 / bias 0.08; Grit g 0.8 / bias 0.05 + 0.3 * level; Crush g 0.8 + depth/4 / bias 0.15
// Max ratio adds depth/8 to g in every colour (distortion grows with the gain reduction)
double Processor::colorProcess(int ch, double x, double env, double depthDb) {
    const int color = static_cast<int>(target_[Color] + 0.5);
    double g, b;
    if (color == 0) { g = 0.5; b = 0.08; }
    else if (color == 1) { g = 0.8; b = 0.05 + 0.3 * std::clamp(env / 0.7, 0.0, 1.0); }
    else { g = 0.8 + depthDb / 4.0; b = 0.15; }
    if (max_) g += depthDb / 8.0;
    const size_t c = static_cast<size_t>(ch);
    double up[2];
    color_.a[c].up(x, up);
    if (color == 2) {
        for (double& s : up) {
            double u4[2];
            color_.b[c].up(s, u4);
            for (double& t : u4) t = shape(t, g, b, color_.dc4[c], dcA4_);
            s = color_.b[c].down(u4);
        }
    } else {
        for (double& s : up) s = shape(s, g, b, color_.dc2[c], dcA2_);
    }
    return color_.a[c].down(up);
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const double bite = target_[Bite] / 100.0;
    for (int i = 0; i < n; ++i) {
        if (drive_.isSmoothing()) gin_ = std::pow(10.0, drive_.next() / 20.0);
        double xin[2] = {0, 0}, level = 0, mono = 0;
        for (int c = 0; c < nch; ++c) {
            xin[c] = ch[c][i] * gin_;
            mono = std::max(mono, std::abs(xin[c]));
            const double key = hpfOn_ ? hpf_[static_cast<size_t>(c)].process(xin[c]) : xin[c];
            level = std::max(level, std::abs(key));
        }
        const double target = comp_.gainDb(20.0 * std::log10(std::max(level, 1e-9)));
        gr_ = ball_.process(target);
        double gr = gr_;
        if (bite > 0.0) {
            // onset = fast envelope 6 dB above the slow one; the gain reduction is relaxed for 5..15 ms after it
            fastEnv_ = std::max(mono, envRel_ * fastEnv_);
            slowEnv_ = mono + envSlow_ * (slowEnv_ - mono);
            const bool on = fastEnv_ > 2.0 * slowEnv_ && fastEnv_ > 1e-4;
            if (on && !onset_) hold_ = static_cast<int>((0.005 + 0.010 * bite) * fs_);
            onset_ = on;
            const double want = hold_ > 0 ? 1.0 - 0.8 * bite : 1.0;
            if (hold_ > 0) --hold_;
            relax_ = want + relaxCoef_ * (relax_ - want);
            gr *= relax_;
        } else {
            relax_ = 1.0;
        }
        const double g = std::pow(10.0, gr / 20.0), depth = std::max(0.0, -gr);
        for (int c = 0; c < nch; ++c) {
            double y = colorProcess(c, xin[c] * g, std::abs(xin[c] * g), depth);
            if (!std::isfinite(y)) y = 0.0;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::dy01
