#include "rs04/rs04.hpp"
#include <algorithm>
#include <cmath>

namespace sw::rs04 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"rs04.target", "Target",      0, 2, 2,    Curve::Step, 1, {0, 1, 2}, "", {"Click", "Crackle", "Both"}},
        {"rs04.sens",   "Sensitivity", 0, 2, 1,    Curve::Step, 1, {0, 1, 2}, "", {"Low", "Mid", "High"}},
        {"rs04.width",  "Click width", 0.1, 5, 1,  Curve::Log, 1, {}, "ms"},
        {"rs04.crackle", "Crackle",    0, 100, 40, Curve::Lin, 1, {}, "%"},
        {"rs04.guard",  "Low guard",   0, 1, 1,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

namespace {
constexpr double kTClick[3] = {7.0, 5.0, 3.5}, kTCrackle[3] = {4.5, 3.5, 2.8}, kAccept = 1.0, kGuardRatio = 0.9, kGuardFactor = 1.6, kCrackleMs = 0.2;
constexpr size_t kRing = 4096;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& c : ch_) c.ring.assign(kRing, 0.0);
    w_.assign(kWin, 0.0); win_.assign(kWin, 0.0); e_.assign(kWin, 0.0); med_.assign(kWin, 0.0); tmp_.assign(kWin, 0.0);
    arWorkPrepare(work_, kLatency, kOrder);
    t_ = 0; sinceHop_ = 0; count_ = 0; prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

int Processor::pass(std::vector<double>& w, double sigma, double T, int maxM, double amount, int rstart, int rend) {
    int repairs = 0;
    for (int i = kOrder; i < kWin; ++i) e_[static_cast<size_t>(i)] = arResidual(w.data(), i, a_, kOrder);
    int i = rstart;
    while (i < rend) {
        if (std::abs(e_[static_cast<size_t>(i)]) <= T * sigma) { ++i; continue; }
        const int s = i;
        int m = 1, mm = 1; bool accepted = false;
        for (;;) {
            mm = std::min(m, maxM);
            const int len = mm + 2 * kOrder;
            for (int k = 0; k < len; ++k) tmp_[static_cast<size_t>(k)] = w[static_cast<size_t>(s - kOrder + k)];
            const bool ok = arInterpolate(tmp_.data(), kOrder, mm, a_, kOrder, work_);
            double worst = 0.0; for (int j = kOrder; j < len; ++j) worst = std::max(worst, std::abs(arResidual(tmp_.data(), j, a_, kOrder)));
            if (ok && worst <= kAccept * T * sigma) { accepted = true; break; }
            if (mm >= maxM) break;
            m *= 2;
        }
        if (!accepted) { i = s + 1 + maxM; continue; }   // wider than Click width (or not a click at all): left as it is
        for (int k = 0; k < mm; ++k) { double& v = w[static_cast<size_t>(s + k)]; v += amount * (tmp_[static_cast<size_t>(kOrder + k)] - v); }
        lo_ = std::min(lo_, s); hi_ = std::max(hi_, s + mm);
        for (int j = std::max(kOrder, s); j < std::min(kWin, s + mm + kOrder); ++j) e_[static_cast<size_t>(j)] = arResidual(w.data(), j, a_, kOrder);
        ++repairs;
        i = s + mm;
    }
    return repairs;
}

void Processor::analyse(Chan& c, int64_t t) {
    const int64_t base = t - kWin;
    for (int i = 0; i < kWin; ++i) w_[static_cast<size_t>(i)] = c.ring[static_cast<size_t>((base + i) % static_cast<int64_t>(kRing))];
    const double err = arFit(w_.data(), kWin, kOrder, a_, win_.data(), r_);
    if (err <= 0.0) return;
    for (int i = kOrder; i < kWin; ++i) { e_[static_cast<size_t>(i)] = arResidual(w_.data(), i, a_, kOrder); med_[static_cast<size_t>(i)] = std::abs(e_[static_cast<size_t>(i)]); }
    auto mid = med_.begin() + kOrder + (kWin - kOrder) / 2;
    std::nth_element(med_.begin() + kOrder, mid, med_.end());
    const double sigma = std::max(*mid / 0.6745, 1e-7);
    const int sens = std::clamp(static_cast<int>(target_[Sensitivity] + 0.5), 0, 2), tg = std::clamp(static_cast<int>(target_[Target] + 0.5), 0, 2);
    double factor = 1.0;
    if (target_[LowGuard] > 0.5) {
        const double k = 1.0 - std::exp(-2.0 * 3.14159265358979323846 * 150.0 / fs_); double lp = 0.0, el = 0.0, et = 0.0;
        for (int i = 0; i < kWin; ++i) { lp += k * (w_[static_cast<size_t>(i)] - lp); el += lp * lp; et += w_[static_cast<size_t>(i)] * w_[static_cast<size_t>(i)]; }
        if (et > 0.0 && el / et > kGuardRatio) factor = kGuardFactor;
    }
    const int rstart = kWin - kLatency, rend = rstart + kHop;
    const int maxClick = std::clamp(static_cast<int>(std::lround(target_[ClickWidth] * 0.001 * fs_)), 1, kLatency - kHop - kOrder);
    const int maxCrackle = std::max(1, static_cast<int>(std::lround(kCrackleMs * 0.001 * fs_)));
    lo_ = kWin; hi_ = 0;
    int n = 0;
    if (tg != CrackleOnly) n += pass(w_, sigma, kTClick[sens] * factor, maxClick, 1.0, rstart, rend);
    if (tg != ClickOnly) n += pass(w_, sigma, kTCrackle[sens] * factor, maxCrackle, target_[Crackle] * 0.01, rstart, rend);
    count_ += n;
    for (int i = lo_; i < hi_; ++i) c.ring[static_cast<size_t>((base + i) % static_cast<int64_t>(kRing))] = w_[static_cast<size_t>(i)];
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    for (int i = 0; i < n; ++i) {
        for (int c = 0; c < nch; ++c) {
            auto& r = ch_[static_cast<size_t>(c)].ring;
            r[static_cast<size_t>(t_ % static_cast<int64_t>(kRing))] = ch[c][i];
            const int64_t q = t_ - kLatency;
            double y = q >= 0 ? r[static_cast<size_t>(q % static_cast<int64_t>(kRing))] : 0.0;
            if (!std::isfinite(y) || std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
        ++t_;
        if (++sinceHop_ >= kHop) { sinceHop_ = 0; if (t_ >= kWin) for (int c = 0; c < nch; ++c) analyse(ch_[static_cast<size_t>(c)], t_); }
    }
}

}  // namespace sw::rs04
