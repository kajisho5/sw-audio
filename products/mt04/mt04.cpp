#include "mt04/mt04.hpp"
#include <algorithm>
#include <cmath>

namespace sw::mt04 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"mt04.persistence", "Persistence", 0.1, 5, 1, Curve::Log, 1, {}, "s"},
        {"mt04.zoom",        "Zoom",        1, 8, 1,   Curve::Step, 1, {1, 2, 4, 8}, "", {"1x", "2x", "4x", "8x"}},
    };
    return s;
}

namespace {
constexpr double kWindow = 0.3, kWarnS = 0.7, kRecover = 0.1;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (int b = 0; b < kBandsN; ++b) { auto& x = bands_[static_cast<size_t>(b)]; x.l.setup(Svf::Mode::BandPass, std::min(bandHz(b), 0.45 * fs_), fs_, 1.4, 0.0); x.r.setup(Svf::Mode::BandPass, std::min(bandHz(b), 0.45 * fs_), fs_, 1.4, 0.0); x.l.reset(); x.r.reset(); x.ll = x.rr = x.lr = x.below = 0.0; }
    ll_ = rr_ = lr_ = sumsq_ = 0.0; warn_ = 0; head_ = 0; count_ = 0; sinceP_ = 0;
    px_.assign(kMaxPoints, 0.0f); py_.assign(kMaxPoints, 0.0f);
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

double Processor::correlation() const { return ll_ > 1e-20 && rr_ > 1e-20 ? std::clamp(lr_ / std::sqrt(ll_ * rr_), -1.0, 1.0) : 0.0; }
double Processor::bandCorrelation(int b) const { const auto& x = bands_[static_cast<size_t>(std::clamp(b, 0, kBandsN - 1))]; return x.ll > 1e-20 && x.rr > 1e-20 ? std::clamp(x.lr / std::sqrt(x.ll * x.rr), -1.0, 1.0) : 0.0; }
double Processor::summedLossDb() const { const double a = ll_ + rr_ + 2.0 * lr_, b = ll_ + rr_; return b > 1e-20 ? 10.0 * std::log10(std::max(a, 1e-20) / b) : 0.0; }

void Processor::scopePoint(int i, double& x, double& y) const {
    const int pos = ((head_ - count_ + i) % kMaxPoints + kMaxPoints) % kMaxPoints; const double z = target_[Zoom];
    x = px_[static_cast<size_t>(pos)] * z; y = py_[static_cast<size_t>(pos)] * z;
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const double k = 1.0 - std::exp(-1.0 / (kWindow * fs_)), r2 = 0.70710678118654752;
    const int keep = std::min(kMaxPoints, std::max(1, static_cast<int>(std::lround(target_[Persistence] * fs_ / kPointStep))));
    for (int i = 0; i < n; ++i) {
        const double l = ch[0][i], r = numCh > 1 ? ch[1][i] : ch[0][i];
        ll_ += k * (l * l - ll_); rr_ += k * (r * r - rr_); lr_ += k * (l * r - lr_);
        for (int b = 0; b < kBandsN; ++b) {
            auto& x = bands_[static_cast<size_t>(b)]; const double bl = x.l.process(l), br = x.r.process(r);
            x.ll += k * (bl * bl - x.ll); x.rr += k * (br * br - x.rr); x.lr += k * (bl * br - x.lr);
            const bool loud = x.ll > 1e-12 && x.rr > 1e-12;   // a band without signal says nothing
            const double c = loud ? x.lr / std::sqrt(x.ll * x.rr) : 1.0;
            if (c < 0.0 && loud) x.below += 1.0 / fs_; else if (c > kRecover || !loud) x.below = 0.0;
            if (x.below >= kWarnS) warn_ |= 1 << b; else if (x.below == 0.0) warn_ &= ~(1 << b);
        }
        if (++sinceP_ >= kPointStep) { sinceP_ = 0; px_[static_cast<size_t>(head_)] = static_cast<float>((l - r) * r2); py_[static_cast<size_t>(head_)] = static_cast<float>((l + r) * r2); head_ = (head_ + 1) % kMaxPoints; count_ = std::min(count_ + 1, keep); }
    }
    count_ = std::min(count_, keep);
}

}  // namespace sw::mt04
