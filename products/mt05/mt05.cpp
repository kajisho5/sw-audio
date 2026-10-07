#include "mt05/mt05.hpp"
#include <algorithm>
#include <cmath>

namespace sw::mt05 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"mt05.ref",   "Ref dBFS", -20, -14, -18, Curve::Step, 1, {-14, -18, -20}, "dBFS", {"-14", "-18", "-20"}},
        {"mt05.meter", "Meter",    0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"VU", "PPM"}},
    };
    return s;
}

namespace {
constexpr double kVuTau = 0.065, kVuForm = 1.1107, kPpmAttack = 0.0045, kPpmFallDbPerS = 20.0 / 1.7;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }
void Processor::prepare(double sampleRate, int) { fs_ = sampleRate; reset(); prepared_ = true; }
void Processor::reset() { vu_.fill(0.0); ppm_.fill(0.0); }
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); }

double Processor::vuDb(int c) const { const double a = vu_[static_cast<size_t>(std::clamp(c, 0, 1))] * kVuForm, ref = std::pow(10.0, target_[RefDb] / 20.0); return a > 1e-9 ? 20.0 * std::log10(a / ref) : -200.0; }
double Processor::ppmDb(int c) const { const double a = ppm_[static_cast<size_t>(std::clamp(c, 0, 1))], ref = std::pow(10.0, target_[RefDb] / 20.0 ) * 1.41421356237; return a > 1e-9 ? 20.0 * std::log10(a / ref) : -200.0; }
double Processor::levelDb(int c) const { return target_[MeterType] > 0.5 ? ppmDb(c) : vuDb(c); }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const double kv = 1.0 - std::exp(-1.0 / (kVuTau * fs_)), kp = 1.0 - std::exp(-1.0 / (kPpmAttack * fs_)), fall = std::pow(10.0, -kPpmFallDbPerS / (20.0 * fs_));
    for (int i = 0; i < n; ++i) for (int c = 0; c < nch; ++c) {
        const double a = std::abs(static_cast<double>(ch[c][i]));
        vu_[static_cast<size_t>(c)] += kv * (a - vu_[static_cast<size_t>(c)]);
        double& p = ppm_[static_cast<size_t>(c)];
        if (a > p) p += kp * (a - p); else p *= fall;
    }
}

}  // namespace sw::mt05
