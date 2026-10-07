#include "cr04/cr04.hpp"
#include <algorithm>
#include <cmath>

namespace sw::cr04 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"cr04.trigger", "Trigger", 0, 2, 0,    Curve::Step, 1, {0, 1, 2}, "", {"Hold", "Momentary", "Auto"}},
        {"cr04.freeze",  "Freeze",  0, 1, 0,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"cr04.blur",    "Blur",    0, 100, 40, Curve::Lin, 1, {}, "%"},
        {"cr04.drift",   "Drift",   0, 2, 1,    Curve::Step, 1, {0, 1, 2}, "", {"Off", "Slow", "Fast"}},
        {"cr04.mix",     "Mix",     0, 100, 50, Curve::Lin, 1, {}, "%"},
    };
    return s;
}

namespace {
constexpr double kPi = 3.14159265358979323846, kDriftSigma[3] = {0.0, 0.1, 0.6}, kSynthScale = 1.0 / 1.5;   // the sum of the squared Hann windows at an overlap of 4
constexpr size_t kOla = 2 * kN;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

double Processor::gauss() {
    auto u = [&]() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return ((rng_ >> 8) + 0.5) * (1.0 / 16777216.0); };
    return std::sqrt(-2.0 * std::log(u())) * std::cos(2.0 * kPi * u());
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    const size_t sz = static_cast<size_t>(1) << 14; mask_ = sz - 1;
    for (auto& r : ring_) r.assign(sz, 0.0f);
    fft_.setup(kN); work_.assign(kN, {0.0, 0.0}); win_.resize(kN);
    for (int i = 0; i < kN; ++i) win_[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / kN);
    const size_t nb = kN / 2 + 1;
    peaksTmp_.clear(); peaksTmp_.reserve(nb);
    for (auto& c : ch_) { c.have = false; c.mag.assign(nb, 0.0); c.magBlur.assign(nb, 0.0); c.omega.assign(nb, 0.0); c.phi.assign(nb, 0.0); c.peakOf.assign(nb, 0); c.ola.assign(kOla, 0.0f); }
    wpos_ = 0; t_ = 0; a_ = 0.0; fade_ = 1.0; env_ = slow_ = 0.0; lastOnset_ = -1000000; captures_ = 0; blurDone_ = -1; wantOn_ = false; pending_ = false;
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::capture(int64_t t0) {
    const size_t nb = kN / 2 + 1;
    for (int ci = 0; ci < 2; ++ci) {
        auto& c = ch_[static_cast<size_t>(ci)];
        for (int i = 0; i < kN; ++i) work_[static_cast<size_t>(i)] = {win_[static_cast<size_t>(i)] * ring_[static_cast<size_t>(ci)][(wpos_ + mask_ + 1 - static_cast<size_t>(kN) + static_cast<size_t>(i)) & mask_], 0.0};
        fft_.forward(work_);
        double mx = 0.0;
        for (size_t k = 0; k < nb; ++k) { c.mag[k] = std::abs(work_[k]); mx = std::max(mx, c.mag[k]); }
        std::fill(c.ola.begin(), c.ola.end(), 0.0f);
        if (mx < 1e-9) { c.have = false; continue; }
        // peaks and the regions of the bins
        std::vector<int>& pk = peaksTmp_; pk.clear();
        for (size_t k = 1; k + 1 < nb; ++k) if (c.mag[k] > c.mag[k - 1] && c.mag[k] >= c.mag[k + 1] && c.mag[k] > 0.01 * mx) pk.push_back(static_cast<int>(k));
        if (pk.empty()) pk.push_back(static_cast<int>(std::max_element(c.mag.begin(), c.mag.end()) - c.mag.begin()));
        for (size_t k = 0; k < nb; ++k) {
            size_t best = 0; int bd = 1 << 30;
            for (size_t j = 0; j < pk.size(); ++j) { const int d = std::abs(static_cast<int>(k) - pk[j]); if (d < bd) { bd = d; best = j; } else if (d > bd) break; }
            c.peakOf[k] = pk[best];
        }
        for (int p : pk) {
            const double a = std::log(c.mag[static_cast<size_t>(p - 1)] + 1e-30), b = std::log(c.mag[static_cast<size_t>(p)] + 1e-30), d = std::log(c.mag[static_cast<size_t>(p + 1)] + 1e-30), den = a - 2.0 * b + d;
            const double off = den < -1e-9 ? std::clamp(0.5 * (a - d) / den, -0.5, 0.5) : 0.0;
            c.omega[static_cast<size_t>(p)] = 2.0 * kPi * (p + off) / kN;
            // the phase at the start of the first synthesis frame (t0 - 3 hop): the capture frame began at t0 - N
            c.phi[static_cast<size_t>(p)] = std::arg(work_[static_cast<size_t>(p)]) + c.omega[static_cast<size_t>(p)] * (kN - 3 * kHop);
        }
        c.have = true;
    }
    blurDone_ = -1;   // the blurred magnitudes are made in frameFor
    nextFrame_ = t0 - 3 * kHop;   // process() generates the frames from here: the four that cover t0 come at once
    ++captures_;
}

void Processor::frameFor(Chan& c, int64_t start, bool) {
    const size_t nb = kN / 2 + 1;
    const int B = static_cast<int>(std::lround(target_[Blur] * 0.01 * 24.0));
    if (blurDone_ != B) {
        for (auto& cc : ch_) { if (!cc.have) continue; for (size_t k = 0; k < nb; ++k) { double sum = 0; int n = 0; for (int d = -B; d <= B; ++d) { const int j = static_cast<int>(k) + d; if (j >= 0 && j < static_cast<int>(nb)) { sum += cc.mag[static_cast<size_t>(j)]; ++n; } } cc.magBlur[k] = sum / std::max(1, n); }
            double e0 = 0, e1 = 0; for (size_t k = 0; k < nb; ++k) { e0 += cc.mag[k] * cc.mag[k]; e1 += cc.magBlur[k] * cc.magBlur[k]; }
            if (e1 > 0) { const double g = std::sqrt(e0 / e1); for (size_t k = 0; k < nb; ++k) cc.magBlur[k] *= g; }   // the power stays: Blur spreads it, it does not take it away
        }
        blurDone_ = B;
    }
    for (size_t k = 0; k < nb; ++k) {
        const int p = c.peakOf[k]; const double ph = c.phi[static_cast<size_t>(p)] + ((static_cast<int>(k) - p) & 1 ? kPi : 0.0);
        work_[k] = std::polar(c.magBlur[k], ph);
    }
    work_[0] = {work_[0].real(), 0.0}; work_[kN / 2] = {work_[kN / 2].real(), 0.0};
    for (int k = 1; k < kN / 2; ++k) work_[static_cast<size_t>(kN - k)] = std::conj(work_[static_cast<size_t>(k)]);
    fft_.inverse(work_);
    for (int n = 0; n < kN; ++n) {
        const int64_t idx = start + n; if (idx < t_) continue;   // already played
        c.ola[static_cast<size_t>(idx) % kOla] += static_cast<float>(kSynthScale * win_[static_cast<size_t>(n)] * work_[static_cast<size_t>(n)].real());
    }
    // the phases of the peaks move on by one hop
    const double sigma = kDriftSigma[std::clamp(static_cast<int>(target_[Drift] + 0.5), 0, 2)] + target_[Blur] * 0.01 * 0.2;
    for (size_t k = 1; k + 1 < nb; ++k) if (c.peakOf[k] == static_cast<int>(k)) { c.phi[k] += c.omega[k] * kHop + (sigma > 0.0 ? sigma * gauss() : 0.0); }
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const int trig = static_cast<int>(target_[Trigger] + 0.5);
    const double mix = target_[Mix] * 0.01, attack = 1.0 / (0.02 * fs_), release = 1.0 / ((trig == Momentary ? 0.04 : 0.4) * fs_), swapStep = 1.0 / (0.015 * fs_);
    const double ea = 1.0 - std::exp(-1.0 / (0.005 * fs_)), es = 1.0 - std::exp(-1.0 / (0.1 * fs_));
    for (int i = 0; i < n; ++i) {
        double dry[2] = {0, 0}; for (int c = 0; c < nch; ++c) dry[c] = ch[c][i];
        const bool on = target_[Freeze] > 0.5;
        // the trigger
        const double lvl = std::abs(dry[0]) + (nch > 1 ? std::abs(dry[1]) : 0.0);
        env_ += ea * (lvl - env_); slow_ += es * (lvl - slow_);
        if (on && trig == AutoTrig && env_ > 2.8 * slow_ && env_ > 3e-3 && t_ - lastOnset_ > static_cast<int64_t>(0.15 * fs_)) { lastOnset_ = t_; pending_ = true; }
        if (on && !wantOn_ && trig != AutoTrig) capture(t_);
        wantOn_ = on;
        if (pending_) { fade_ -= swapStep; if (fade_ <= 0.0) { fade_ = 0.0; capture(t_); pending_ = false; } } else if (fade_ < 1.0) fade_ = std::min(1.0, fade_ + swapStep);
        const bool have = ch_[0].have || ch_[1].have;
        const bool active = on && have;
        if (active) a_ = std::min(1.0, a_ + attack); else a_ = std::max(0.0, a_ - release);
        while (have && nextFrame_ <= t_) { for (int c = 0; c < nch; ++c) if (ch_[static_cast<size_t>(c)].have) frameFor(ch_[static_cast<size_t>(c)], nextFrame_, true); nextFrame_ += kHop; }
        for (int c = 0; c < nch; ++c) {
            auto& cc = ch_[static_cast<size_t>(c)];
            float& o = cc.ola[static_cast<size_t>(t_) % kOla]; const double wet = static_cast<double>(o) * fade_; o = 0.0f;
            double y = dry[c] * (1.0 - mix * a_) + wet * mix * a_;
            if (!std::isfinite(y) || std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
        for (int c = 0; c < nch; ++c) ring_[static_cast<size_t>(c)][wpos_] = static_cast<float>(dry[c]);
        if (nch == 1) ring_[1][wpos_] = static_cast<float>(dry[0]);
        wpos_ = (wpos_ + 1) & mask_; ++t_;
    }
}

}  // namespace sw::cr04
