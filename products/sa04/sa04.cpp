#include "sa04/sa04.hpp"
#include <algorithm>
#include <cmath>

namespace sw::sa04 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"sa04.iron",      "Iron",       0, 2, 1,    Curve::Step, 1, {0, 1, 2}, "", {"Nickel", "Steel", "Mu"}},
            {"sa04.gain",      "Gain",       0, 60, 30,  Curve::Lin,  1, {}, ""},
            {"sa04.load",      "Load",       0, 1, 0.5,  Curve::Lin,  1, {}, ""},
            {"sa04.lowweight", "Low weight", 0, 10, 0,   Curve::Lin,  1, {}, ""},
            {"sa04.topair",    "Top air",    0, 10, 0,   Curve::Lin,  1, {}, ""},
            {"sa04.output",    "Output",     -10, 10, 0, Curve::Lin,  1, {}, "dB"},
            {"sa04.pad",       "Pad",        0, 1, 0,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            oversampleSpec("sa04.os"),
            unitSpec("sa04.unit"),
        };
        v[Load].minLabel = "Low"; v[Load].maxLabel = "High";
        return v;
    }();
    return s;
}

namespace {
// per iron: saturation ceiling of the highs (the lows get a third of it), bias, low-frequency corner (Hz) (design values)
struct IronModel { double ceiling, bias, lfHz; };
constexpr IronModel kIron[3] = {{3.0, 0.03, 8.0}, {2.0, 0.05, 12.0}, {1.0, 0.10, 18.0}};   // Nickel / Steel / Mu
constexpr double kSplitHz = 150.0, kPi = 3.14159265358979323846;
double dbOf(double knob, bool pad) { return (knob - 30.0) - (pad ? 20.0 : 0.0); }
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    lo_.prepare(fs_); hi_.prepare(fs_); lo_.setOversample(static_cast<int>(target_[Oversample])); hi_.setOversample(static_cast<int>(target_[Oversample]));
    applyUnit();
    lp_ = {0, 0}; lpA_ = 1.0 - std::exp(-2.0 * kPi * kSplitHz / fs_);
    gain_.reset(fs_, 20.0, std::pow(10.0, dbOf(target_[Gain], target_[Pad] > 0.5) / 20.0));
    updateFilters();
}

void Processor::applyUnit() {
    unit_ = static_cast<int>(target_[Unit]);
    for (int c = 0; c < 2; ++c) { lo_.setOnsetDb(c, sw::Unit::satDb(unit_, c, 0)); hi_.setOnsetDb(c, sw::Unit::satDb(unit_, c, 1)); }   // where each saturation sets in, per channel
}

void Processor::updateFilters() {
    const IronModel& m = kIron[static_cast<int>(target_[Iron] + 0.5)];
    const double load = target_[Load];
    const double resHz = std::min(26000.0 - 12000.0 * load, 0.45 * fs_), resDb = 4.0 * load;
    for (int k = 0; k < 2; ++k) {   // each channel's parts have their own tolerance (Unit B / C)
        Ch& c = f_[static_cast<size_t>(k)];
        auto fm = [&](int slot, double f) { return std::min(f * sw::Unit::freqMul(unit_, k, slot), 0.45 * fs_); };
        c.weight.setup(Svf::Mode::LowShelf, fm(0, 120.0), fs_, 0.70710678, 0.6 * target_[LowWeight]);
        c.air.setup(Svf::Mode::HighShelf, fm(1, 8000.0), fs_, 0.70710678, 0.6 * target_[TopAir]);
        c.hp.setup(Svf::Mode::HighPass, fm(2, m.lfHz * (1.0 + 1.5 * load)), fs_, 0.5, 0);
        c.lfLoss.setup(Svf::Mode::LowShelf, fm(3, 50.0), fs_, 0.70710678, -1.5 * load);
        c.res.setup(Svf::Mode::Bell, fm(4, resHz), fs_, 1.2, resDb);
    }
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Gain || id == Pad) gain_.setTarget(std::pow(10.0, dbOf(target_[Gain], target_[Pad] > 0.5) / 20.0));
    else if (id == Oversample) { lo_.setOversample(static_cast<int>(v)); hi_.setOversample(static_cast<int>(v)); }
    else if (id == Unit) { applyUnit(); updateFilters(); }
    else updateFilters();
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const IronModel& m = kIron[static_cast<int>(target_[Iron] + 0.5)];
    for (int i = 0; i < n; ++i) {
        const double g = gain_.next();
        for (int c = 0; c < nch; ++c) {
            const size_t cc = static_cast<size_t>(c);
            Ch& f = f_[cc];
            double x = ch[c][i] * g;
            x = f.air.process(f.weight.process(x));
            x = f.res.process(f.lfLoss.process(f.hp.process(x)));
            lp_[cc] += lpA_ * (x - lp_[cc]);
            const double lo = lp_[cc], hi = x - lo;
            double y = lo_.process(c, lo, 1.0, m.bias, m.ceiling / 3.0) + hi_.process(c, hi, 1.0, m.bias * 0.5, m.ceiling);
            if (!std::isfinite(y)) y = 0.0;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::sa04
