#include "rv08/rv08.hpp"
#include <algorithm>
#include <cmath>

namespace sw::rv08 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"rv08.size",      "Size",      0, 10, 5,      Curve::Lin, 1, {}, ""},
        {"rv08.gatetime",  "Gate time", 50, 800, 250,  Curve::Log, 1, {}, "ms"},
        {"rv08.threshold", "Threshold", 0, 10, 5,      Curve::Lin, 1, {}, ""},
        {"rv08.shape",     "Shape",     0, 100, 0,     Curve::Lin, 1, {}, "%"},
        {"rv08.tone",      "Tone",      0, 100, 50,    Curve::Lin, 1, {}, "%"},
        {"rv08.mix",       "Mix",       0, 100, 40,    Curve::Lin, 1, {}, "%"},
        {"rv08.evo.on",    "Snare key", 0, 1, 0,       Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}
double thresholdDbfs(double v) { return -60.0 + 6.0 * std::clamp(v, 0.0, 10.0); }
double gateGain(double x, double shape) { const double s = std::clamp(shape, 0.0, 1.0); return (1.0 - s) + s * std::clamp(x, 0.0, 1.0); }

namespace {
constexpr double kHysteresisDb = 6.0, kCloseMs = 3.0;
double decaySeconds(double size) { return 0.6 + 0.2 * size; }
double lengthScale(double size) { return std::pow(2.0, (size - 5.0) / 5.0); }
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::updateLines() {
    const int n = fdn_.lines();
    const double k = lengthScale(target_[Size]);
    for (int i = 0; i < n; ++i) fdn_.setLength(i, 20.0 * std::pow(65.0 / 20.0, static_cast<double>(i) / (n - 1)) * k * 0.001 * fs_);
    fdn_.setModulation(3.0, 0.4);
    fdn_.setDecay(decaySeconds(target_[Size]));
    fdn_.setDamping(10000.0);
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    fdn_.prepare(fs_, 16, 0.2);
    for (int k = 0; k < 4; ++k) { static const double ms[4] = {3.1, 2.3, 8.3, 5.9}; ap_[static_cast<size_t>(k)].buf.assign(std::max<size_t>(1, static_cast<size_t>(std::lround(ms[k] * 0.001 * fs_))), 0.0f); ap_[static_cast<size_t>(k)].pos = 0; }
    for (auto& f : keyLow_) f.setup(Svf::Mode::BandPass, 200.0, fs_, 2.0, 0.0);   // 150 .. 250 Hz (two in series)
    for (int k = 0; k < 2; ++k) { keyHigh_[static_cast<size_t>(k)].setup(Svf::Mode::HighPass, 2000.0, fs_, 0.7071, 0.0); keyHigh_[static_cast<size_t>(k + 2)].setup(Svf::Mode::LowPass, 5000.0, fs_, 0.7071, 0.0); }   // 2 .. 5 kHz
    env_ = 0.0; gate_ = 0.0; t_ = 0.0; open_ = false; armed_ = true; tiltLp_[0] = tiltLp_[1] = 0.0;
    updateLines(); fdn_.snapLengths();
    lateTrim_ = 1.0 / std::sqrt(Fdn::kEnergyConstant * decaySeconds(target_[Size]) / fdn_.meanLengthSeconds());
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (prepared_ && id == Size) updateLines();
}

void Processor::snapToTargets() {
    if (!prepared_) return;
    updateLines(); fdn_.snapLengths();
    lateTrim_ = 1.0 / std::sqrt(Fdn::kEnergyConstant * decaySeconds(target_[Size]) / fdn_.meanLengthSeconds());
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const double thr = std::pow(10.0, thresholdDbfs(target_[Threshold]) / 20.0), thrLow = thr * std::pow(10.0, -kHysteresisDb / 20.0);
    const double gateSamples = target_[GateTime] * 0.001 * fs_, closeStep = 1.0 / (kCloseMs * 0.001 * fs_);
    const double shape = target_[Shape] * 0.01, tone = (target_[Tone] - 50.0) * 0.02;
    const double gh = std::pow(10.0, tone * 6.0 / 20.0), gl = 1.0 / gh, lpA = 1.0 - std::exp(-2.0 * 3.14159265358979323846 * 1000.0 / fs_);
    const bool key = target_[Snare] > 0.5;
    const double envA = 1.0 - std::exp(-1.0 / (0.005 * fs_)), envR = 1.0 - std::exp(-1.0 / (0.015 * fs_));
    const double trimTarget = 1.0 / std::sqrt(Fdn::kEnergyConstant * decaySeconds(target_[Size]) / fdn_.meanLengthSeconds());
    for (int i = 0; i < n; ++i) {
        lateTrim_ += 0.0005 * (trimTarget - lateTrim_);
        const double m = nch > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i];
        double det = m;
        if (key) {
            double lo = keyLow_[1].process(keyLow_[0].process(m)), hi = keyHigh_[3].process(keyHigh_[2].process(keyHigh_[1].process(keyHigh_[0].process(m))));
            det = lo + hi;
        }
        const double a = std::abs(det);
        env_ += (a > env_ ? envA : envR) * (a - env_);
        if (env_ < thrLow) armed_ = true;
        if (env_ > thr && armed_) { armed_ = false; open_ = true; t_ = 0.0; }
        double target = 0.0;
        if (open_) {
            target = gateGain(t_ / gateSamples, shape);
            t_ += 1.0;
            if (t_ >= gateSamples) open_ = false;
        }
        // rise follows the shape; closing takes 3 ms
        if (target >= gate_) gate_ = target; else gate_ = std::max(target, gate_ - closeStep);
        double d = m;
        for (auto& ap : ap_) d = ap.process(d, 0.6);
        double fl, fr; fdn_.process(d, fl, fr);
        const double w[2] = {fl * lateTrim_ * gate_, fr * lateTrim_ * gate_};
        for (int c = 0; c < nch; ++c) {
            tiltLp_[c] += lpA * (w[c] - tiltLp_[c]);
            double y = gl * tiltLp_[c] + gh * (w[c] - tiltLp_[c]);
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::rv08
