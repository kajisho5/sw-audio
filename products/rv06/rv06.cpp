#include "rv06/rv06.hpp"
#include <algorithm>
#include <cmath>

namespace sw::rv06 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"rv06.decay",    "Decay",    1, 60, 12,   Curve::Log, 1, {}, "s"},
        {"rv06.shimmer",  "Shimmer",  0, 100, 60,  Curve::Lin, 1, {}, "%"},
        {"rv06.interval", "Interval", 0, 2, 0,     Curve::Step, 1, {0, 1, 2}, "", {"Octave", "Fifth", "Both"}},
        {"rv06.mix",      "Mix",      0, 100, 35,  Curve::Lin, 1, {}, "%"},
        {"rv06.evo.on",   "Freeze",   0, 1, 0,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"rv06.duck",     "Duck",     0, 1, 1,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}
namespace {
constexpr double kShiftRatio[2] = {2.0, 1.5};
constexpr double kShimmerMaxAmount = 0.7;   // share of a line that is shifted at Shimmer 100 %
constexpr double kDuckDb = -6.0;
double dampingHz() { return 9000.0; }   // the tail keeps its brightness: what climbs must still be heard
}

// two grains of a 50 ms window, half a window apart, crossfaded with sin^2 / cos^2; both read the delay line faster than it is written (pitch up)
double Processor::Shifter::process(double x, double ratio) {
    buf[pos] = static_cast<float>(x);
    const double w = win, sz = static_cast<double>(buf.size());
    const double d2 = std::fmod(d + 0.5 * w, w);
    auto tap = [&](double dd) {
        double rp = static_cast<double>(pos) - dd; if (rp < 0) rp += sz;
        const size_t i0 = static_cast<size_t>(rp) % buf.size(), i1 = (i0 + 1) % buf.size(); const double fr = rp - std::floor(rp);
        return buf[i0] + fr * (buf[i1] - buf[i0]);
    };
    const double wa = std::sin(3.14159265358979323846 * d / w), wb = std::sin(3.14159265358979323846 * d2 / w);
    const double y = wa * wa * tap(d) + wb * wb * tap(d2);
    d -= ratio - 1.0; if (d < 0.0) d += w;
    pos = (pos + 1) % buf.size();
    return y;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::updateLines() {
    const int n = fdn_.lines();
    for (int i = 0; i < n; ++i) fdn_.setLength(i, 25.0 * std::pow(75.0 / 25.0, static_cast<double>(i) / (n - 1)) * 0.001 * fs_);
    fdn_.setModulation(5.0, 0.3);
    fdn_.setDecay(target_[Decay]);
    fdn_.setDamping(dampingHz());
    fdn_.setFreeze(target_[Freeze] > 0.5);
}

double Processor::process(int line, double s) {
    if ((line & 1) != 0 || shimAmount_ <= 0.0) return s;
    const int idx = line >> 1;
    const double ratio = interval_ == Octave ? kShiftRatio[0] : interval_ == Fifth ? kShiftRatio[1] : kShiftRatio[idx & 1];
    const double y = shifter_[static_cast<size_t>(idx)].process(s, ratio);
    return s + shimAmount_ * (y - s);
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    fdn_.prepare(fs_, 16, 0.2);
    fdn_.setHook(this);
    for (int k = 0; k < 4; ++k) { static const double ms[4] = {3.0, 2.2, 7.9, 5.8}; ap_[static_cast<size_t>(k)].buf.assign(std::max<size_t>(1, static_cast<size_t>(std::lround(ms[k] * 0.001 * fs_))), 0.0f); ap_[static_cast<size_t>(k)].pos = 0; }
    for (size_t k = 0; k < shifter_.size(); ++k) { shifter_[k].prepare(fs_); shifter_[k].d = shifter_[k].win * static_cast<double>(k) / static_cast<double>(shifter_.size()); }   // different grain phases
    env_ = 0.0; duckGain_ = 1.0;
    updateLines(); fdn_.snapLengths();
    lateTrim_ = 1.0 / std::sqrt(Fdn::kEnergyConstant * target_[Decay] / fdn_.meanLengthSeconds());
    interval_ = static_cast<int>(target_[Interval] + 0.5);
    shimAmount_ = shimTarget_ = target_[Freeze] > 0.5 ? 0.0 : kShimmerMaxAmount * target_[Shimmer] * 0.01;
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (!prepared_) return;
    if (id == Decay || id == Freeze) updateLines();
}

void Processor::snapToTargets() {
    if (!prepared_) return;
    updateLines(); fdn_.snapLengths();
    lateTrim_ = 1.0 / std::sqrt(Fdn::kEnergyConstant * target_[Decay] / fdn_.meanLengthSeconds());
    interval_ = static_cast<int>(target_[Interval] + 0.5);
    shimAmount_ = shimTarget_ = target_[Freeze] > 0.5 ? 0.0 : kShimmerMaxAmount * target_[Shimmer] * 0.01;
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    fdn_.setHook(this);   // (here, not only in prepare: a copied or moved Processor must not leave the FDN calling the old object)
    const int nch = std::min(numCh, 2);
    const bool frozen = target_[Freeze] > 0.5, duck = target_[Duck] > 0.5;
    interval_ = static_cast<int>(target_[Interval] + 0.5);
    shimTarget_ = frozen ? 0.0 : kShimmerMaxAmount * target_[Shimmer] * 0.01;
    const double trimTarget = 1.0 / std::sqrt(Fdn::kEnergyConstant * target_[Decay] / fdn_.meanLengthSeconds());
    const double envA = 1.0 - std::exp(-1.0 / (0.005 * fs_)), envR = 1.0 - std::exp(-1.0 / (0.15 * fs_));
    const double dgA = 1.0 - std::exp(-1.0 / (0.01 * fs_)), dgR = 1.0 - std::exp(-1.0 / (0.25 * fs_));
    for (int i = 0; i < n; ++i) {
        shimAmount_ += 0.0005 * (shimTarget_ - shimAmount_);
        lateTrim_ += 0.0005 * (trimTarget - lateTrim_);
        const double m = nch > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i];
        const double am = std::abs(m);
        env_ += (am > env_ ? envA : envR) * (am - env_);
        const double k = std::clamp((20.0 * std::log10(env_ + 1e-9) + 40.0) / 30.0, 0.0, 1.0);
        const double dgTarget = duck ? std::pow(10.0, kDuckDb * k / 20.0) : 1.0;
        duckGain_ += (dgTarget < duckGain_ ? dgA : dgR) * (dgTarget - duckGain_);
        double d = frozen ? 0.0 : m;
        for (auto& a : ap_) d = a.process(d, 0.6);
        double fl, fr; fdn_.process(d, fl, fr);
        double l = fl * lateTrim_ * duckGain_, r = fr * lateTrim_ * duckGain_;
        if (std::abs(l) < 1e-30) l = 0.0; if (std::abs(r) < 1e-30) r = 0.0;
        ch[0][i] = static_cast<float>(l);
        if (nch > 1) ch[1][i] = static_cast<float>(r);
    }
}

}  // namespace sw::rv06
