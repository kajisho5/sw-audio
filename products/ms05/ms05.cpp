#include "ms05/ms05.hpp"
#include <algorithm>
#include <cmath>

namespace sw::ms05 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"ms05.target", "Target", -40, -6, -18, Curve::Lin,  1, {}, "LUFS"},
        {"ms05.range",  "Range",  0, 24, 6,     Curve::Lin,  1, {}, "dB"},
        {"ms05.speed",  "Speed",  0, 2, 1,      Curve::Step, 1, {0, 1, 2}, "", {"Slow", "Medium", "Fast"}},
        {"ms05.gate",   "Gate",   -80, -20, -50, Curve::Lin, 1, {}, "dBFS"},
        {"ms05.source", "Source", 0, 2, 0,      Curve::Step, 1, {0, 1, 2}, "", {"Vocal", "Mix", "Bass"}},
        {"ms05.ride",   "Ride",   -24, 24, 0,   Curve::Lin,  1, {}, "dB"},
        {"ms05.evo.on", "Write automation", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}, nullptr, nullptr, 1.0, false},
    };
    return s;
}

namespace { constexpr double kTau[3] = {3.0, 1.0, 0.3}; }   // Speed: Slow / Medium / Fast, seconds (design values)

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::updateSource() {
    const int src = static_cast<int>(target_[Source] + 0.5);
    for (size_t c = 0; c < 2; ++c)
        for (size_t k = 0; k < 2; ++k) {
            hp_[c][k].setup(Svf::Mode::HighPass, 150.0, fs_, 0.70710678, 0);
            lp_[c][k].setup(Svf::Mode::LowPass, src == 2 ? 250.0 : std::min(5000.0, fs_ * 0.45), fs_, 0.70710678, 0);
        }
    msC_ = std::exp(-1.0 / ((src == 2 ? 0.4 : 0.2) * fs_));   // exponential windows: 200 ms (about a 400 ms block), Bass 400 ms
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& k : k_) k.setup(fs_);
    msLoud_ = msRaw_ = 0;
    rideDb_ = target_[Write] > 0.5 ? 0.0 : target_[Ride];
    gainFrom_ = gainTo_ = std::pow(10.0, rideDb_ / 20.0);
    open_ = false; dirty_ = false; sent_ = rideDb_;
    updateSource();
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Source) updateSource();
    else if (id == Ride && target_[Write] < 0.5) rideDb_ = v;   // host automation drives it while we are not writing
    else if (id == Write && v < 0.5) rideDb_ = target_[Ride];
}

int Processor::takeParamWrite(int& id, double& plain) {
    int f = 0;
    const bool writing = target_[Write] > 0.5;
    if (writing && !open_) { f |= 1; open_ = true; dirty_ = true; }
    if (open_ && (dirty_ || !writing)) { f |= 2; plain = rideDb_; id = Ride; sent_ = rideDb_; dirty_ = false; }
    if (!writing && open_) { f |= 4; open_ = false; }
    return f;
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const bool writing = target_[Write] > 0.5;
    const double tau = kTau[static_cast<int>(target_[Speed] + 0.5)], range = target_[Range];
    const int src = static_cast<int>(target_[Source] + 0.5);
    for (int start = 0; start < n; start += kControl) {
        const int len = std::min(kControl, n - start);
        double kSum = 0, rawSum = 0;
        for (int i = start; i < start + len; ++i)
            for (int c = 0; c < nch; ++c) {
                const double x = ch[c][i];
                double w = k_[static_cast<size_t>(c)].process(x);
                if (src != 1) { auto& h = hp_[static_cast<size_t>(c)]; auto& l = lp_[static_cast<size_t>(c)]; w = l[1].process(l[0].process(src == 2 ? w : h[1].process(h[0].process(w)))); }
                kSum += w * w; rawSum += x * x;
            }
        // exponential mean squares, advanced by len samples
        const double a = std::pow(msC_, len);
        msLoud_ = a * msLoud_ + (1.0 - a) * kSum / len;
        msRaw_ = a * msRaw_ + (1.0 - a) * rawSum / (len * std::max(1, nch));
        if (writing) {
            const double levelDb = msRaw_ > 1e-20 ? 10.0 * std::log10(msRaw_) : -200.0;
            if (levelDb > target_[Gate]) {
                const double lufs = LoudnessMeter::lufs(msLoud_);
                const double want = std::clamp(target_[Target] - lufs, -range, range);
                rideDb_ += (want - rideDb_) * (1.0 - std::exp(-len / (tau * fs_)));
            }
            rideDb_ = std::clamp(rideDb_, -range, range);
            if (std::abs(rideDb_ - sent_) >= 0.02) dirty_ = true;
        }
        gainFrom_ = gainTo_; gainTo_ = std::pow(10.0, rideDb_ / 20.0);
        for (int i = 0; i < len; ++i) {
            const double g = gainFrom_ + (gainTo_ - gainFrom_) * (i + 1) / len;
            for (int c = 0; c < nch; ++c) {
                double y = ch[c][start + i] * g;
                if (!std::isfinite(y)) y = 0.0;
                if (std::abs(y) < 1e-30) y = 0.0;
                ch[c][start + i] = static_cast<float>(y);
            }
        }
    }
}

}  // namespace sw::ms05
