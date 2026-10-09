#include "rs05/rs05.hpp"
#include <algorithm>
#include <cmath>

namespace sw::rs05 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"rs05.thresh",  "Threshold", -3, 0, -0.5, Curve::Lin, 1, {}, "dB"},
        {"rs05.quality", "Quality",   0, 2, 2,     Curve::Step, 1, {0, 1, 2}, "", {"Low", "Mid", "High"}},
        {"rs05.makeup",  "Makeup",    -12, 0, -3,  Curve::Lin, 1, {}, "dB"},
        {"rs05.smooth",  "Smooth",    0, 2, 1,     Curve::Step, 1, {0, 1, 2}, "", {"Low", "Mid", "High"}},
        {"rs05.evo.on",  "Detect",    0, 1, 1,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

namespace {
constexpr int kOrders[3] = {16, 32, 64};
constexpr double kOverDb[3] = {9.0, 6.0, 3.0}, kPile = 0.995;
constexpr size_t kRing = 8192;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& c : ch_) { c.ring.assign(kRing, 0.0); c.raw.assign(kRing, 0.0); c.autoLevel = 0.0; c.autoAge = 1 << 30; }
    w_.assign(kWin, 0.0); win_.assign(kWin, 0.0); tmp_.assign(kWin, 0.0); runsAll_.reserve(256);
    arWorkPrepare(work_, kMaxRun, 64);
    t_ = 0; sinceHop_ = 0; count_ = 0; used_ = 1.0; prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::analyse(Chan& c, int64_t t) {
    const int64_t base = t - kWin;
    for (int i = 0; i < kWin; ++i) w_[static_cast<size_t>(i)] = c.ring[static_cast<size_t>((base + i) % static_cast<int64_t>(kRing))];
    const int q = std::clamp(static_cast<int>(target_[Quality] + 0.5), 0, 2), sm = std::clamp(static_cast<int>(target_[Smooth] + 0.5), 0, 2), p = kOrders[q];
    double clip = std::pow(10.0, target_[Threshold] / 20.0);
    c.autoAge += kHop;
    if (target_[Detect] > 0.5) {
        // the ceiling is read from the samples as they came in (the window in `ring` holds repaired peaks)
        for (int i = 0; i < kWin; ++i) win_[static_cast<size_t>(i)] = c.raw[static_cast<size_t>((base + i) % static_cast<int64_t>(kRing))];
        double m = 0.0; for (int i = 0; i < kWin; ++i) m = std::max(m, std::abs(win_[static_cast<size_t>(i)]));
        if (m > 0.05) {   // a pile-up at the maximum: at least 4 runs of 2 or more samples equal to it (to 1e-4): the flat tops of hard clipping (a smooth peak has about one sample that close)
            int runs = 0, len = 0;
            for (int i = 0; i < kWin; ++i) { if (std::abs(win_[static_cast<size_t>(i)]) >= m * (1.0 - 1e-4)) { if (++len == 2) ++runs; } else len = 0; }
            if (runs >= 4) { const double found = kPile * m; c.autoLevel = c.autoAge >= static_cast<int>(fs_) ? found : 0.9 * c.autoLevel + 0.1 * found; c.autoAge = 0; }
        }
        if (c.autoAge < static_cast<int>(fs_) && c.autoLevel > 0.0) clip = c.autoLevel;
    }
    if (&c == &ch_[0]) used_ = clip;
    const double cap = clip * std::pow(10.0, kOverDb[sm] / 20.0);
    const int rstart = kWin - kLatency, rend = rstart + kHop;
    // 1. the runs that start in the region about to leave
    Run runs[32]; int nr = 0;
    for (int i = rstart; i < rend && nr < 32;) {
        const double v = w_[static_cast<size_t>(i)];
        if (std::abs(v) < clip) { ++i; continue; }
        const bool pos = v > 0.0; int j = i;
        while (j < kWin && std::abs(w_[static_cast<size_t>(j)]) >= clip && ((w_[static_cast<size_t>(j)] > 0.0) == pos)) ++j;
        const int m = j - i;
        if (m >= 2 && m <= kMaxRun && j + p <= kWin && i >= p) runs[nr++] = {i, m, pos};
        i = j;
    }
    int lo = kWin, hi = 0;
    if (nr > 0) {
        // 2. every run (also the ones outside the region, in the window) is estimated with a low order first; the model of the wanted order is fitted to that estimate (the flat tops of the
        //    clipped signal would be learnt otherwise) and the runs are interpolated again from the original samples
        std::vector<double>& orig = tmp_;
        for (int i = 0; i < kWin; ++i) orig[static_cast<size_t>(i)] = w_[static_cast<size_t>(i)];
        auto allRuns = [&](std::vector<Run>& out) { for (int i = p; i < kWin;) { const double v = orig[static_cast<size_t>(i)]; if (std::abs(v) < clip) { ++i; continue; } const bool pos = v > 0.0; int j = i; while (j < kWin && std::abs(orig[static_cast<size_t>(j)]) >= clip && ((orig[static_cast<size_t>(j)] > 0.0) == pos)) ++j; if (j - i >= 2 && j - i <= kMaxRun && j + p <= kWin) out.push_back({i, j - i, pos}); i = j; } };
        std::vector<Run>& all = runsAll_; all.clear(); allRuns(all);
        auto interpolateAll = [&](int order) {
            arFit(w_.data(), kWin, order, a_, win_.data(), r_);
            for (const Run& r : all) {
                for (int k = 0; k < r.m; ++k) w_[static_cast<size_t>(r.s + k)] = orig[static_cast<size_t>(r.s + k)];
                if (!arInterpolate(w_.data(), r.s, r.m, a_, order, work_)) { for (int k = 0; k < r.m; ++k) w_[static_cast<size_t>(r.s + k)] = orig[static_cast<size_t>(r.s + k)]; continue; }
                const double sg = r.pos ? 1.0 : -1.0;
                for (int k = 0; k < r.m; ++k) { double& x = w_[static_cast<size_t>(r.s + k)]; x = sg * std::min(std::max(sg * x, clip), cap); }
            }
        };
        const int first = std::min(p, 16);
        interpolateAll(first);
        if (p > first) interpolateAll(p);
        for (int n = 0; n < nr; ++n) { lo = std::min(lo, runs[n].s); hi = std::max(hi, runs[n].s + runs[n].m); ++count_; }
    }
    for (int k = lo; k < hi; ++k) c.ring[static_cast<size_t>((base + k) % static_cast<int64_t>(kRing))] = w_[static_cast<size_t>(k)];
}

void Processor::scope(double* in, double* out) const {
    for (int b = 0; b < kScopeBins; ++b) in[b] = out[b] = 0.0;
    const int64_t q = t_ - kLatency;   // the samples before q have left the plug-in
    if (!prepared_ || q < kScopeWin) return;
    const int per = kScopeWin / kScopeBins;
    const Chan& c = ch_[0];
    for (int b = 0; b < kScopeBins; ++b)
        for (int k = 0; k < per; ++k) {
            const size_t i = static_cast<size_t>((q - kScopeWin + b * per + k) % static_cast<int64_t>(kRing));
            if (std::abs(c.raw[i]) > std::abs(in[b])) in[b] = c.raw[i];
            if (std::abs(c.ring[i]) > std::abs(out[b])) out[b] = c.ring[i];
        }
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const double g = std::pow(10.0, target_[Makeup] / 20.0);
    for (int i = 0; i < n; ++i) {
        for (int c = 0; c < nch; ++c) {
            auto& r = ch_[static_cast<size_t>(c)].ring;
            r[static_cast<size_t>(t_ % static_cast<int64_t>(kRing))] = ch[c][i];
            ch_[static_cast<size_t>(c)].raw[static_cast<size_t>(t_ % static_cast<int64_t>(kRing))] = ch[c][i];
            const int64_t q = t_ - kLatency;
            double y = q >= 0 ? r[static_cast<size_t>(q % static_cast<int64_t>(kRing))] * g : 0.0;
            if (!std::isfinite(y) || std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
        ++t_;
        if (++sinceHop_ >= kHop) { sinceHop_ = 0; if (t_ >= kWin) for (int c = 0; c < nch; ++c) analyse(ch_[static_cast<size_t>(c)], t_); }
    }
}

}  // namespace sw::rs05
