#include "rs07/rs07.hpp"
#include <algorithm>
#include <cmath>

namespace sw::rs07 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"rs07.sens",  "Sensitivity", 0, 2, 1,     Curve::Step, 1, {0, 1, 2}, "", {"Low", "Mid", "High"}},
        {"rs07.size",  "Click size",  0, 2, 0,     Curve::Step, 1, {0, 1, 2}, "", {"Small", "Medium", "Large"}},
        {"rs07.skew",  "Freq skew",   -50, 50, 0,  Curve::Lin, 1, {}, "%"},
        {"rs07.fade",  "Fade",        0.5, 10, 2,  Curve::Log, 1, {}, "ms"},
    };
    return s;
}

namespace {
constexpr double kT[3] = {6.0, 4.5, 3.5}, kMaxMs[3] = {0.3, 0.6, 1.0}, kGapDb = 12.0, kPeakFall = 3.0;
constexpr size_t kRing = 4096;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& c : ch_) { c.ring.assign(kRing, 0.0); c.peakDb = -200.0; }
    w_.assign(kWin, 0.0); win_.assign(kWin, 0.0); e_.assign(kWin, 0.0); med_.assign(kWin, 0.0); tmp_.assign(kWin, 0.0);
    arWorkPrepare(work_, kLatency, kOrder);
    t_ = 0; sinceHop_ = 0; count_ = 0; prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::analyse(Chan& c, int64_t t) {
    const int64_t base = t - kWin;
    for (int i = 0; i < kWin; ++i) w_[static_cast<size_t>(i)] = c.ring[static_cast<size_t>((base + i) % static_cast<int64_t>(kRing))];
    const double err = arFit(w_.data(), kWin, kOrder, a_, win_.data(), r_);
    if (err <= 0.0) return;
    const double s = target_[FreqSkew] * 0.01;
    auto weighted = [&](int i) { const double e = arResidual(w_.data(), i, a_, kOrder), e1 = i > kOrder ? arResidual(w_.data(), i - 1, a_, kOrder) : e; return s >= 0 ? e + s * (e - e1) : e + (-s) * (e + e1); };
    for (int i = kOrder; i < kWin; ++i) { e_[static_cast<size_t>(i)] = weighted(i); med_[static_cast<size_t>(i)] = std::abs(e_[static_cast<size_t>(i)]); }
    auto mid = med_.begin() + kOrder + (kWin - kOrder) / 2;
    std::nth_element(med_.begin() + kOrder, mid, med_.end());
    const double sigma = std::max(*mid / 0.6745, 1e-7);
    // the phrase peak: the level (dBFS) of the last 40 ms
    { double e = 0; const int n40 = std::min(kWin, static_cast<int>(0.04 * fs_)); for (int i = kWin - n40; i < kWin; ++i) e += w_[static_cast<size_t>(i)] * w_[static_cast<size_t>(i)]; const double db = e > 1e-20 ? 10.0 * std::log10(e / n40) : -200.0; c.peakDb = std::max(db, c.peakDb - kPeakFall * kHop / fs_); }
    const int sens = std::clamp(static_cast<int>(target_[Sensitivity] + 0.5), 0, 2), size = std::clamp(static_cast<int>(target_[ClickSize] + 0.5), 0, 2);
    const double T = kT[sens];
    const int maxM = std::clamp(static_cast<int>(std::lround(kMaxMs[size] * 0.001 * fs_)), 1, (kLatency - kHop - kOrder) / 7);
    const int fadeHalf = static_cast<int>(std::lround(0.5 * target_[Fade] * 0.001 * fs_));
    const int rstart = kWin - kLatency, rend = rstart + kHop;
    lo_ = kWin; hi_ = 0;
    int i = rstart;
    while (i < rend) {
        if (std::abs(e_[static_cast<size_t>(i)]) <= T * sigma) { ++i; continue; }
        const int s0 = i;
        // is it in a gap? the 40 ms around it, without the first maxM samples of the click
        double el = 0; int cnt = 0; const int half = static_cast<int>(0.02 * fs_);
        for (int j = std::max(0, s0 - half); j < std::min(kWin, s0 + half); ++j) { if (j >= s0 && j < s0 + maxM) continue; el += w_[static_cast<size_t>(j)] * w_[static_cast<size_t>(j)]; ++cnt; }
        const double localDb = (cnt > 0 && el > 1e-20) ? 10.0 * std::log10(el / cnt) : -200.0;
        if (localDb > c.peakDb - kGapDb) { i = s0 + 1 + maxM; continue; }   // inside a word
        int m = 1, mm = 1; bool accepted = false;
        for (;;) {
            mm = std::min(m, maxM);
            const int ext = 0, st = s0 - ext, len = mm + 2 * ext;
            if (st < kOrder || st + len + kOrder > kWin) break;
            for (int k = 0; k < len + 2 * kOrder; ++k) tmp_[static_cast<size_t>(k)] = w_[static_cast<size_t>(st - kOrder + k)];
            const bool ok = arInterpolate(tmp_.data(), kOrder, len, a_, kOrder, work_);
            double worst = 0.0; for (int j = kOrder; j < len + 2 * kOrder; ++j) worst = std::max(worst, std::abs(arResidual(tmp_.data(), j, a_, kOrder)));
            if (ok && worst <= T * sigma) { accepted = true; break; }
            if (mm >= maxM) break;
            m *= 2;
        }
        if (!accepted) { i = s0 + 1 + maxM; continue; }
        // the length is found; the repair itself starts and ends Fade/2 further out, on clean signal
        for (int k = 0; k < mm; ++k) win_[static_cast<size_t>(k)] = tmp_[static_cast<size_t>(kOrder + k)];   // the interpolation of the click alone
        const int ext = std::max(0, std::min({fadeHalf, 60, s0 - kOrder, kWin - kOrder - mm - s0 - 1})), st = s0 - ext, len = mm + 2 * ext;
        for (int k = 0; k < len + 2 * kOrder; ++k) tmp_[static_cast<size_t>(k)] = w_[static_cast<size_t>(st - kOrder + k)];
        if (ext == 0 || !arInterpolate(tmp_.data(), kOrder, len, a_, kOrder, work_)) {
            for (int k = 0; k < len + 2 * kOrder; ++k) tmp_[static_cast<size_t>(k)] = w_[static_cast<size_t>(st - kOrder + k)];
            for (int k = 0; k < mm; ++k) tmp_[static_cast<size_t>(kOrder + ext + k)] = win_[static_cast<size_t>(k)];
        }
        for (int k = 0; k < len; ++k) w_[static_cast<size_t>(st + k)] = tmp_[static_cast<size_t>(kOrder + k)];
        lo_ = std::min(lo_, st); hi_ = std::max(hi_, st + len);
        for (int j = std::max(kOrder, st); j < std::min(kWin, st + len + kOrder); ++j) e_[static_cast<size_t>(j)] = weighted(j);
        ++count_;
        i = s0 + mm;
    }
    for (int k = lo_; k < hi_; ++k) c.ring[static_cast<size_t>((base + k) % static_cast<int64_t>(kRing))] = w_[static_cast<size_t>(k)];
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

}  // namespace sw::rs07
