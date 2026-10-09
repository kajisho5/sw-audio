#include "st06/st06.hpp"
#include <algorithm>
#include <cmath>

namespace sw::st06 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"st06.frequency", "Frequency",  20, 300, 120, Curve::Log, 1, {}, "Hz"},
        {"st06.slope",     "Slope",      6, 48, 24, Curve::Step, 1, {6, 12, 24, 48}, "dB/oct", {"6", "12", "24", "48"}},
        {"st06.sideboost", "Side boost", -6, 6, 0, Curve::Lin, 1, {}, "dB"},
        {"st06.output",    "Output",     -10, 10, 0, Curve::Lin, 1, {}, "dB"},
        {"st06.listen",    "Listen",     0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}, nullptr, nullptr, 1.0, false},
            unitSpec("st06.unit"),
    };
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846;
// Butterworth Q of section k of an order-2m filter
double butterQ(int order, int k) { return 1.0 / (2.0 * std::cos((2.0 * k + 1.0) * kPi / (2.0 * order))); }
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::update() {
    const double f = target_[Frequency], sl = target_[Slope], b = target_[SideBoost];
    if (f != freq_ || sl != slope_) {
        const int order = static_cast<int>(std::lround(sl / 6.0));   // 1, 2, 4, 8
        sections_ = order == 1 ? 0 : order / 2;
        for (int k = 0; k < sections_; ++k) { hp_[static_cast<size_t>(k)].setup(Svf::Mode::HighPass, f, fs_, butterQ(order, k), 0.0); lp_[static_cast<size_t>(k)].setup(Svf::Mode::LowPass, f, fs_, butterQ(order, k), 0.0); }
        hpOne_ = std::exp(-2.0 * kPi * f / fs_);
        freq_ = f; slope_ = sl;
        // the shelf only moves when its own settings do
        boost_ = -99.0;
    }
    if (b != boost_) { shelf_.setup(Svf::Mode::HighShelf, f * 2.0, fs_, 0.7071, b); boost_ = b; }
    outT_ = std::pow(10.0, target_[Output] / 20.0);
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& h : hp_) h.reset();
    for (auto& h : lp_) h.reset();
    shelf_.reset(); hpState_ = 0.0; freq_ = slope_ = boost_ = -99.0;
    update(); outG_ = outT_;
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::snapToTargets() { if (prepared_) { update(); outG_ = outT_; } }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 2) return;
    update();
    const bool listen = target_[Listen] > 0.5, boost = target_[SideBoost] != 0.0;
    const double k = 1.0 - std::exp(-1.0 / (0.01 * fs_));
    for (int i = 0; i < n; ++i) {
        outG_ += k * (outT_ - outG_);
        const double m = 0.5 * (ch[0][i] + ch[1][i]), s = 0.5 * (ch[0][i] - ch[1][i]);
        double hs, ls = 0.0;
        if (sections_ == 0) { hpState_ = (1.0 - hpOne_) * s + hpOne_ * hpState_; hs = s - hpState_; ls = hpState_; }   // 6 dB/oct: s minus its one-pole low-pass
        else { hs = s; for (int q = 0; q < sections_; ++q) hs = hp_[static_cast<size_t>(q)].process(hs); if (listen) { ls = s; for (int q = 0; q < sections_; ++q) ls = lp_[static_cast<size_t>(q)].process(ls); } }
        double a, b;
        if (listen) { a = b = ls; }
        else {
            if (boost) hs = shelf_.process(hs);
            a = (m + hs) * outG_; b = (m - hs) * outG_;
        }
        if (std::abs(a) < 1e-30) a = 0.0;
        if (std::abs(b) < 1e-30) b = 0.0;
        ch[0][i] = static_cast<float>(a); ch[1][i] = static_cast<float>(b);
    }
}

}  // namespace sw::st06
