#include "lv08/lv08.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv08 {
namespace { constexpr double kBias = 1.5; }   // the minimum of a smoothed power sits below its mean: +1.8 dB

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv08.reduction", "Reduction",   -30, 0, -18, Curve::Lin, 1, {}, "dB"},
        {"lv08.sens",      "Sensitivity", 0, 2, 1,     Curve::Step, 1, {0, 1, 2}, "", {"Low", "Mid", "High"}},
        {"lv08.guard",     "Voice guard", 0, 2, 2,     Curve::Step, 1, {0, 1, 2}, "", {"Low", "Mid", "High"}},
        {"lv08.keyboard",  "Keyboard",    0, 1, 1,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"lv08.hvac",      "HVAC",        0, 1, 1,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int maxBlock) {
    fs_ = sampleRate; stft_.prepare(kLatency, kHop, 2); vd_.prepare(fs_);
    const size_t nb = kLatency / 2 + 1; ps_.assign(nb, 0.0); pn_.assign(nb, 0.0); nt_.assign(nb, 0.0); gs_.assign(nb, 1.0); learnedP_.assign(nb, 0.0); acc_.assign(nb, 0.0);
    warm_ = static_cast<int>(0.3 * fs_ / kHop); kbHold_ = 0; hfAvg_ = 0; minGDb_ = 0; learned_ = learning_ = false;
    riseF_ = std::pow(10.0, 3.0 / 10.0 * kHop / fs_);   // 3 dB per second in power
    mono_.assign(static_cast<size_t>(std::max(1, maxBlock) + 8), 0.0f); prepared_ = true;
}
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    if (static_cast<size_t>(n) > mono_.size()) mono_.assign(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i) mono_[static_cast<size_t>(i)] = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
    // the voice detector is fed up to the frame (not the whole host block first): what frame() reads is the same whatever the block size is
    const int nc = std::min(numCh, 2);
    for (int off = 0; off < n;) {
        const int m = std::min(n - off, stft_.toNextFrame());
        vd_.process(mono_.data() + off, m);
        float* p[2] = {ch[0] + off, nc > 1 ? ch[1] + off : nullptr};
        stft_.process(p, nc, m, *this);
        off += m;
    }
}

void Processor::frame(std::complex<double>* const* spec, int nch, int nbins) {
    const double binHz = fs_ / kLatency;
    const double redMin = std::pow(10.0, std::min(0.0, target_[Reduction]) / 20.0);
    const int sens = static_cast<int>(target_[Sensitivity]), guard = static_cast<int>(target_[VoiceGuard]);
    static constexpr double kA[3] = {1.0, 1.6, 2.4}, kGuardDb[3] = {-30.0, -10.0, -5.0};
    const double a = kA[std::clamp(sens, 0, 2)], guardMin = std::pow(10.0, kGuardDb[std::clamp(guard, 0, 2)] / 20.0);
    const bool voice = vd_.active();
    double hf = 0;
    for (int k = 0; k < nbins; ++k) {
        double p = 0; for (int c = 0; c < nch; ++c) p += std::norm(spec[c][k]); p /= nch;
        const size_t i = static_cast<size_t>(k);
        ps_[i] = 0.6 * ps_[i] + 0.4 * p;
        pn_[i] = 0.93 * pn_[i] + 0.07 * p;
        if (warm_ > 0) nt_[i] = pn_[i]; else nt_[i] = pn_[i] < nt_[i] ? pn_[i] : nt_[i] * riseF_;
        if (learning_) acc_[i] += p;
        if (k * binHz >= 2000.0) hf += p;
    }
    if (warm_ > 0) --warm_;
    if (learning_ && ++learnFrames_ >= static_cast<int>(2.0 * fs_ / kHop)) { for (size_t i = 0; i < acc_.size(); ++i) learnedP_[i] = acc_[i] / learnFrames_; learned_ = true; learning_ = false; }
    // keyboard
    if (hfAvg_ <= 0) hfAvg_ = hf;
    const bool strike = target_[Keyboard] > 0.5 && !voice && hf > 15.8 * hfAvg_ && hf > 1e-6 * nbins;   // +12 dB
    if (strike) kbHold_ = static_cast<int>(0.053 * fs_ / kHop);
    else if (kbHold_ > 0) --kbHold_;
    hfAvg_ += (hf - hfAvg_) * (1.0 - std::exp(-static_cast<double>(kHop) / (0.2 * fs_)));
    double minG = 1.0;
    for (int k = 0; k < nbins; ++k) {
        const size_t i = static_cast<size_t>(k); const double f = k * binHz;
        double g = 1.0;
        if (target_[Hvac] > 0.5) {
            const double N = learned_ ? std::max(learnedP_[i], 0.0) : kBias * nt_[i];
            g = ps_[i] > 1e-18 ? std::clamp(1.0 - a * N / ps_[i], redMin, 1.0) : redMin;
            if (voice && f >= 200.0 && f <= 4000.0) g = std::max(g, guardMin);
        }
        if (kbHold_ > 0 && f > 1500.0) g = std::min(g, redMin);
        g = std::max(g, redMin);
        gs_[i] = g < gs_[i] ? 0.5 * gs_[i] + 0.5 * g : 0.88 * gs_[i] + 0.12 * g;
        if (std::abs(gs_[i] - 1.0) < 1e-6) gs_[i] = 1.0;
        for (int c = 0; c < nch; ++c) spec[c][k] *= gs_[i];
        minG = std::min(minG, gs_[i]);
    }
    minGDb_ = 20.0 * std::log10(std::max(minG, 1e-6));
}

}  // namespace sw::lv08
