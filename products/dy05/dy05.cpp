#include "dy05/dy05.hpp"
#include <algorithm>
#include <cmath>

namespace sw::dy05 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"dy05.mode",      "Mode",      0, 1, 1,        Curve::Step, 1, {0, 1}, "", {"Wide", "Split"}},
        {"dy05.freq",      "Freq",      2000, 16000, 6500, Curve::Log, 1, {}, "Hz"},
        {"dy05.thresh",    "Threshold", -60, 0, -24,    Curve::Lin,  1, {}, "dB"},
        {"dy05.range",     "Range",     -24, 0, -8,     Curve::Lin,  1, {}, "dB"},
        {"dy05.lookahead", "Lookahead", 0, 5, 2,        Curve::Lin,  1, {}, "ms"},
        {"dy05.listen",    "Listen",    0, 1, 0,        Curve::Step, 1, {0, 1}, "", {"Off", "On"}, nullptr, nullptr, 1.0, false},
        {"dy05.evo.on",    "Pitch follow", 0, 1, 1,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

namespace {
// design values
constexpr double kRatioLowDb = -14.0, kRatioHighDb = -6.0;  // HF share (dB) where sibilance weight goes 0 -> 1
constexpr double kOverDb = 6.0;                              // HF level above Threshold for the full Range
constexpr double kVoicedWeight = 0.25;                       // Pitch follow: weight in voiced spans
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

int Processor::latencySamples() const { return static_cast<int>(std::lround(fs_ * target_[Lookahead] * 0.001)); }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    lat_ = latencySamples();
    for (auto& v : dx_) v.assign(static_cast<size_t>(std::max(1, lat_)), 0.0f);
    for (auto& v : dh_) v.assign(static_cast<size_t>(std::max(1, lat_)), 0.0f);
    for (auto& v : dl_) v.assign(static_cast<size_t>(std::max(1, lat_)), 0.0f);
    pos_ = 0;
    energyC_ = Ballistics::coef(fs_, 4.0);
    attackC_ = Ballistics::coef(fs_, 0.5);
    releaseC_ = Ballistics::coef(fs_, 40.0);
    pitch_.prepare(fs_);
    gr_ = hfE_ = fullE_ = 0; voicedW_ = 1.0;
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Freq)
        for (size_t c = 0; c < 2; ++c)
            for (size_t k = 0; k < 2; ++k) {
                hp_[c][k].setup(Svf::Mode::HighPass, std::min(v, fs_ * 0.45), fs_, 0.70710678, 0);
                lp_[c][k].setup(Svf::Mode::LowPass, std::min(v, fs_ * 0.45), fs_, 0.70710678, 0);
            }
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const bool wide = target_[Mode] < 0.5, listen = target_[Listen] > 0.5, follow = target_[Pitch] > 0.5;
    const double thr = target_[Threshold], range = target_[Range];
    const size_t len = static_cast<size_t>(std::max(1, lat_));
    for (int i = 0; i < n; ++i) {
        double hf[2] = {0, 0}, lf[2] = {0, 0}, x[2] = {0, 0}, hfPow = 0, fullPow = 0, mono = 0;
        for (int c = 0; c < nch; ++c) {
            x[c] = ch[c][i];
            hf[c] = hp_[static_cast<size_t>(c)][1].process(hp_[static_cast<size_t>(c)][0].process(x[c]));  // LR4 high-pass
            lf[c] = lp_[static_cast<size_t>(c)][1].process(lp_[static_cast<size_t>(c)][0].process(x[c]));
            hfPow += hf[c] * hf[c]; fullPow += x[c] * x[c]; mono += x[c];
        }
        hfE_ = hfPow + energyC_ * (hfE_ - hfPow);
        fullE_ = fullPow + energyC_ * (fullE_ - fullPow);
        if (follow) pitch_.push(mono / nch);
        const double share = 10.0 * std::log10((hfE_ + 1e-20) / (fullE_ + 1e-20));
        double w = std::clamp((share - kRatioLowDb) / (kRatioHighDb - kRatioLowDb), 0.0, 1.0);
        w = w * w * (3.0 - 2.0 * w);
        const double wantV = follow && pitch_.voiced() ? kVoicedWeight : 1.0;
        voicedW_ = wantV + energyC_ * (voicedW_ - wantV);
        const double over = 10.0 * std::log10(hfE_ / nch + 1e-20) - thr;
        const double target = range * std::clamp(over / kOverDb, 0.0, 1.0) * w * voicedW_;
        gr_ = target < gr_ ? target + attackC_ * (gr_ - target) : target + releaseC_ * (gr_ - target);
        const double g = gr_ > -1e-6 ? 1.0 : std::pow(10.0, gr_ / 20.0);
        const size_t rd = static_cast<size_t>(pos_);      // slot written `lat_` samples ago
        for (int c = 0; c < nch; ++c) {
            auto& bx = dx_[static_cast<size_t>(c)]; auto& bh = dh_[static_cast<size_t>(c)]; auto& bl = dl_[static_cast<size_t>(c)];
            double xd = x[c], hd = hf[c], ld = lf[c];
            if (lat_ > 0) { xd = bx[rd]; hd = bh[rd]; ld = bl[rd]; bx[rd] = static_cast<float>(x[c]); bh[rd] = static_cast<float>(hf[c]); bl[rd] = static_cast<float>(lf[c]); }
            double y = listen ? hd : wide ? g * xd : ld + g * hd;
            if (!std::isfinite(y)) y = 0.0;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
        pos_ = static_cast<int>((static_cast<size_t>(pos_) + 1) % len);
    }
}

}  // namespace sw::dy05
