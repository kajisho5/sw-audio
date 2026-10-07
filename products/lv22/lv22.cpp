#include "lv22/lv22.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv22 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv22.mode",   "Mode",        0, 2, 0,    Curve::Step, 1, {0, 1, 2}, "", {"Mic vs mic", "Speaker", "Line"}},
        {"lv22.window", "Window",      50, 1000, 200, Curve::Log, 1, {}, "ms"},
        {"lv22.hold",   "Hold result", 0, 1, 1,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate; dec_ = std::max(1, static_cast<int>(std::lround(fs_ / 12000.0))); fd_ = fs_ / dec_;
    ring_ = static_cast<int>(std::ceil(1.0 * fd_)) + 8; fft_.setup(kN);
    a_.assign(static_cast<size_t>(ring_), 0.0f); b_.assign(static_cast<size_t>(ring_), 0.0f); fa_.assign(kN, {}); fb_.assign(kN, {});
    pos_ = 0; since_ = 0; decN_ = 0; accA_ = accB_ = 0; result_ = Unknown; corr_ = lagMs_ = 0; prepared_ = true;
}
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); }

void Processor::processWithSidechain(float** ch, int numCh, int n, const float* const* sc, int scCh) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    const bool useSc = sc && scCh > 0 && sc[0];
    const double updateS = std::max(0.1, 0.5 * target_[Window] * 0.001);
    for (int i = 0; i < n; ++i) {
        double ta, tb;
        if (useSc) { ta = numCh > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i]; tb = scCh > 1 && sc[1] ? 0.5 * (sc[0][i] + sc[1][i]) : sc[0][i]; }
        else { ta = ch[0][i]; tb = numCh > 1 ? ch[1][i] : 0.0; }
        accA_ += ta; accB_ += tb;
        if (++decN_ < dec_) continue;
        a_[static_cast<size_t>(pos_)] = static_cast<float>(accA_ / dec_); b_[static_cast<size_t>(pos_)] = static_cast<float>(accB_ / dec_); accA_ = accB_ = 0; decN_ = 0;
        pos_ = (pos_ + 1) % ring_;
        if (++since_ >= static_cast<int>(updateS * fd_)) { since_ = 0; analyse(); }
    }
}

void Processor::analyse() {
    const int W = std::min(ring_ - 8, std::max(16, static_cast<int>(std::lround(target_[Window] * 0.001 * fd_))));
    const int mode = static_cast<int>(target_[Mode] + 0.5);
    const double lagS = mode == Speaker ? 0.060 : mode == Line ? 0.001 : 0.005; const int L = std::min(kN / 2 - 1, std::max(1, static_cast<int>(std::lround(lagS * fd_))));
    std::fill(fa_.begin(), fa_.end(), std::complex<double>{}); std::fill(fb_.begin(), fb_.end(), std::complex<double>{});
    double ea = 0, eb = 0;
    for (int i = 0; i < W; ++i) { const size_t idx = static_cast<size_t>((pos_ - W + i + ring_) % ring_); fa_[static_cast<size_t>(i)] = a_[idx]; fb_[static_cast<size_t>(i)] = b_[idx]; ea += double(a_[idx]) * a_[idx]; eb += double(b_[idx]) * b_[idx]; }
    int res = Unknown; double best = 0, bestLag = 0;
    if (ea > 1e-8 * W && eb > 1e-8 * W) {
        fft_.forward(fa_); fft_.forward(fb_);
        for (int k = 0; k < kN; ++k) fa_[static_cast<size_t>(k)] = fa_[static_cast<size_t>(k)] * std::conj(fb_[static_cast<size_t>(k)]);   // r[l] = sum a[n + l] b[n]
        fft_.inverse(fa_);
        double bv = 0; int bl = 0;
        for (int l = -L; l <= L; ++l) { const double r = fa_[static_cast<size_t>((l + kN) % kN)].real(); if (std::abs(r) > std::abs(bv)) { bv = r; bl = l; } }
        best = bv / std::sqrt(ea * eb); bestLag = bl / fd_ * 1000.0;
        if (std::abs(best) >= 0.3) res = best > 0 ? InPhase : OutOfPhase;
    }
    corr_ = best; lagMs_ = bestLag;
    if (res == Unknown && target_[HoldResult] > 0.5 && result_ != Unknown) return;
    result_ = res;
}

}  // namespace sw::lv22
