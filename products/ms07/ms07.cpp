#include "ms07/ms07.hpp"
#include <algorithm>
#include <cmath>

namespace sw::ms07 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"ms07.bits",     "Bits",   16, 24, 16, Curve::Step, 1, {16, 20, 24}, "", {"16 bit", "20 bit", "24 bit"}},
        {"ms07.shape",    "Shape",  0, 4, 2,    Curve::Step, 1, {0, 1, 2, 3, 4}, "", {"Off", "Light", "Mid", "Strong", "Ultra"}},
        {"ms07.out",      "Output", -10, 10, 0, Curve::Lin,  1, {}, "dB"},
        {"ms07.evo.on",   "Blank",  0, 1, 0,    Curve::Step, 1, {0, 1}, "", {"Auto blank", "Always"}},
    };
    return s;
}

namespace {
// error-feedback taps h for NTF(z) = 1 - sum h_k z^-k = (1 - z^-1)^N
const std::array<std::array<double, 4>, 5> kTaps = {{{0, 0, 0, 0}, {1, 0, 0, 0}, {2, -1, 0, 0}, {3, -3, 1, 0}, {4, -6, 4, -1}}};
constexpr int kBlankAfter = 1024;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    gain_.reset(fs_, 20.0, std::pow(10.0, target_[Output] / 20.0));
    ch_ = {};
    probe_.prepare(fs_);
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Output) gain_.setTarget(std::pow(10.0, v / 20.0));
    if (id == Shape) for (auto& c : ch_) c.e = {};
}

double Processor::tpdf() {  // sum of two uniform [-0.5, 0.5] -> triangular [-1, 1] LSB
    auto uni = [this] { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_ / 4294967296.0 - 0.5; };
    return uni() + uni();
}

void Processor::process(float** chans, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const double q = std::ldexp(1.0, static_cast<int>(target_[Bits]) - 1);
    const auto& h = kTaps[static_cast<size_t>(target_[Shape])];
    const bool autoBlank = target_[Blank] < 0.5;
    const double fadeStep = 1.0 / std::max(1.0, fs_ * 0.002);  // 2 ms
    for (int i = 0; i < n; ++i) {
        const double g = gain_.next();
        if (probe_.listening()) probe_.add(chans[0][i], nch > 1 ? chans[1][i] : chans[0][i]);   // the input as it comes in (Truncation check)
        for (int c = 0; c < nch; ++c) {
            Ch& s = ch_[static_cast<size_t>(c)];
            const float in = chans[c][i];
            if (autoBlank) {
                if (in == 0.0f) {
                    if (++s.zeroRun >= kBlankAfter) { s.e = {}; s.ditherAmp = 0.0; chans[c][i] = 0.0f; continue; }
                } else {
                    s.zeroRun = 0;
                }
            }
            s.ditherAmp = std::min(1.0, s.ditherAmp + fadeStep);
            const double y = in * g;
            const double w = y - (h[0] * s.e[0] + h[1] * s.e[1] + h[2] * s.e[2] + h[3] * s.e[3]);
            double qv = std::round(w * q + s.ditherAmp * tpdf()) / q;
            qv = std::clamp(qv, -1.0, 1.0 - 1.0 / q);
            s.e = {qv - w, s.e[0], s.e[1], s.e[2]};
            chans[c][i] = static_cast<float>(qv);
        }
    }
}

void Processor::check() { if (probe_.listening()) probe_.cancel(); else probe_.start(); }

}  // namespace sw::ms07
