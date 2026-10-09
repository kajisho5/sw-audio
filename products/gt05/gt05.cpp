#include "gt05/gt05.hpp"
#include <algorithm>
#include <cmath>
#include <complex>

namespace sw::gt05 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"gt05.level",     "Level",     -20, 10, 0,  Curve::Lin, 1, {}, "dB"},
            {"gt05.impedance", "Impedance", 10000, 1000000, 1000000, Curve::Step, 1, {10000, 47000, 100000, 1000000}, "ohm", {"10k", "47k", "100k", "1M"}},
            {"gt05.cable",     "Cable",     100, 1000, 100, Curve::Log, 1, {}, "pF"},
            {"gt05.pickup",    "Pickup",    0, 100, 50,  Curve::Lin, 1, {}, "%"},
            {"gt05.output",    "Output",    -10, 10, 0,  Curve::Lin, 1, {}, "dB"},
            {"gt05.evo.on",    "Pickup swap", 0, 1, 0,   Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            unitSpec("gt05.unit"),
        };
        v[Cable].minLabel = "Short"; v[Cable].maxLabel = "Long"; v[Pickup].minLabel = "Single"; v[Pickup].maxLabel = "Hum";
        return v;
    }();
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846;
struct Circuit { double f0, q, g0; };
Circuit circuit(double impedance, double cablePf, double pickupPct) {
    const double p = std::clamp(pickupPct, 0.0, 100.0) * 0.01;
    const double L = 2.5 + 3.0 * p, R = 6000.0 + 3000.0 * p, C = 100e-12 + 80e-12 * p + cablePf * 1e-12;
    const double a = L * C, b = L / impedance + R * C, c = 1.0 + R / impedance;
    return {std::sqrt(c / a) / (2.0 * kPi), std::sqrt(a * c) / b, 1.0 / c};
}
}

double pickupResponseDb(double f, double impedance, double cablePf, double pickupPct) {
    const double p = std::clamp(pickupPct, 0.0, 100.0) * 0.01;
    const double L = 2.5 + 3.0 * p, R = 6000.0 + 3000.0 * p, C = 100e-12 + 80e-12 * p + cablePf * 1e-12;
    const double w = 2.0 * kPi * f;
    const std::complex<double> den(1.0 + R / impedance - L * C * w * w, (L / impedance + R * C) * w);
    return -20.0 * std::log10(std::abs(den));
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::updateCircuit(bool ramp) {
    const Circuit c = circuit(target_[Impedance], target_[Cable], target_[Pickup]);
    const double fc = std::min(c.f0, 0.45 * fs_);
    const int n = static_cast<int>(0.02 * fs_);
    for (auto& r : res_) { if (ramp) r.setupRamp(Svf::Mode::LowPass, fc, fs_, c.q, 0.0, n); else r.setup(Svf::Mode::LowPass, fc, fs_, c.q, 0.0); }
    circuit_.setTarget(c.g0);
    circuitGain_ = c.g0;
}

// the DI's own pickup resonance: the 1/3-octave band between 2 and 6.3 kHz that stands highest above the mean of the bands two steps either side
void Processor::estimate() {
    if (analyzer_.frames() < 20) return;
    double bestP = 0.0; int best = -1;
    for (int b = 20; b <= 25; ++b) {
        const double l0 = analyzer_.levelDb(b), lm = analyzer_.levelDb(b - 2), lp = analyzer_.levelDb(b + 2);
        if (l0 < -150.0 || lm < -150.0 || lp < -150.0) continue;
        const double pk = l0 - 0.5 * (lm + lp);
        if (pk > bestP) { bestP = pk; best = b; }
    }
    if (best < 0 || bestP < 3.0) { estHz_ = 0.0; estDb_ = 0.0; }
    else {
        const double a = analyzer_.levelDb(best - 1), b = analyzer_.levelDb(best), c = analyzer_.levelDb(best + 1);
        const double den = a - 2.0 * b + c;
        const double d = std::abs(den) > 1e-9 ? std::clamp(0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
        estHz_ = ThirdOctaveAnalyzer::centerHz(best) * std::pow(2.0, d / 3.0);
        estDb_ = std::min(bestP, 15.0);
    }
    const double gain = estHz_ > 0.0 ? -0.85 * estDb_ : 0.0;
    for (auto& c : cancel_) c.setupRamp(Svf::Mode::Bell, estHz_ > 0.0 ? estHz_ : 3500.0, fs_, 2.5, gain, static_cast<int>(0.2 * fs_));
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    level_.reset(fs_, 20.0, std::pow(10.0, target_[Level] / 20.0)); output_.reset(fs_, 20.0, std::pow(10.0, target_[Output] / 20.0));
    const Circuit c = circuit(target_[Impedance], target_[Cable], target_[Pickup]);
    circuit_.reset(fs_, 20.0, c.g0);
    updateCircuit(false);
    for (auto& f : cancel_) f.setup(Svf::Mode::Bell, 3500.0, fs_, 2.5, 0.0);
    analyzer_.setup(fs_, 4096, 3.0);
    estHz_ = estDb_ = 0.0; sinceEstimate_ = 0;
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (!prepared_) return;
    switch (id) {
        case Level: level_.setTarget(std::pow(10.0, v / 20.0)); break;
        case Output: output_.setTarget(std::pow(10.0, v / 20.0)); break;
        case Impedance: case Cable: case Pickup: updateCircuit(true); break;
        case PickupSwap:
            if (v < 0.5) { estHz_ = estDb_ = 0.0; for (auto& c : cancel_) c.setupRamp(Svf::Mode::Bell, 3500.0, fs_, 2.5, 0.0, static_cast<int>(0.2 * fs_)); }
            else { analyzer_.reset(); sinceEstimate_ = 0; }
            break;
        default: break;
    }
}

void Processor::snapToTargets() {
    if (!prepared_) return;
    for (LinearSmoother* s : {&level_, &output_, &circuit_}) s->skip(1 << 30);
    updateCircuit(false);
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const bool swap = target_[PickupSwap] > 0.5;
    for (int i = 0; i < n; ++i) {
        const double lv = level_.next(), out = output_.next(), cg = circuit_.next();
        if (swap) {
            double m = 0; for (int c = 0; c < nch; ++c) m += ch[c][i];
            analyzer_.push(m / nch);
            if (++sinceEstimate_ >= static_cast<int>(0.5 * fs_)) { sinceEstimate_ = 0; estimate(); }
        }
        for (int c = 0; c < nch; ++c) {
            double y = ch[c][i] * lv;
            if (swap || estHz_ > 0.0) y = cancel_[static_cast<size_t>(c)].process(y);
            y = res_[static_cast<size_t>(c)].process(y) * cg * out;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::gt05
