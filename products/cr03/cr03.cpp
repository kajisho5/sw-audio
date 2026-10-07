#include "cr03/cr03.hpp"
#include <algorithm>
#include <cmath>

namespace sw::cr03 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"cr03.mode",    "Mode",    0, 2, 0,     Curve::Step, 1, {0, 1, 2}, "", {"Cloud", "Scatter", "Glitch"}},
        {"cr03.grain",   "Grain",   5, 500, 60,  Curve::Log, 1, {}, "ms"},
        {"cr03.density", "Density", 1, 100, 40,  Curve::Log, 1, {}, "/s"},
        {"cr03.spray",   "Spray",   0, 100, 30,  Curve::Lin, 1, {}, "%"},
        {"cr03.pitch",   "Pitch",   -24, 24, 5,  Curve::Lin, 1, {}, "st"},
        {"cr03.spread",  "Spread",  0, 2, 2,     Curve::Step, 1, {0, 1, 2}, "", {"Mono", "Narrow", "Wide"}},
        {"cr03.mix",     "Mix",     0, 100, 50,  Curve::Lin, 1, {}, "%"},
        {"cr03.freeze",  "Freeze input", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"cr03.evo.on",  "Harmony", 0, 1, 1,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kFftN = 8192, kChromaHop = 6000, kBits = 18;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

double Processor::rnd() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return (rng_ >> 8) * (1.0 / 16777216.0); }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    const size_t sz = static_cast<size_t>(1) << kBits; mask_ = sz - 1;
    for (auto& b : buf_) b.assign(sz, 0.0f);
    for (auto& g : grains_) g.on = false;
    fft_.setup(kFftN); work_.assign(kFftN, {0.0, 0.0}); win_.resize(kFftN); mag_.assign(kFftN / 2 + 1, 0.0);
    for (int i = 0; i < kFftN; ++i) win_[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / kFftN);
    wpos_ = 0; t_ = 0; nextIn_ = 0.0; active_ = 0; chord_ = 0; sinceChroma_ = 0; frameCount_ = 0; framePos_ = 0; lastStart_ = 0.0; lastLen_ = 0.0;
    for (auto& c : chroma_) c.fill(0.0);
    frameValid_.fill(false); frameStrongest_.fill(0); frameTime_.fill(0);
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::analyseChroma() {
    // the last kFftN samples (mono), Hann window
    for (int i = 0; i < kFftN; ++i) { const size_t k = (wpos_ + (mask_ + 1) - static_cast<size_t>(kFftN) + static_cast<size_t>(i)) & mask_; work_[static_cast<size_t>(i)] = {win_[static_cast<size_t>(i)] * 0.5 * (buf_[0][k] + buf_[1][k]), 0.0}; }
    fft_.forward(work_);
    const int hi = static_cast<int>(2000.0 * kFftN / fs_), lo = std::max(2, static_cast<int>(60.0 * kFftN / fs_));
    double mx = 0.0;
    for (int k = lo - 1; k <= hi + 1; ++k) { mag_[static_cast<size_t>(k)] = std::norm(work_[static_cast<size_t>(k)]); mx = std::max(mx, mag_[static_cast<size_t>(k)]); }
    auto& c = chroma_[static_cast<size_t>(framePos_)]; c.fill(0.0);
    const double thr = std::pow(1e-3 * kFftN / 4.0, 2.0);   // a sine of -60 dBFS gives (A N / 4)^2 with the Hann window
    const bool valid = mx > thr;
    if (valid) {
        for (int k = lo; k <= hi; ++k) {
            const double p = mag_[static_cast<size_t>(k)];
            if (p > mag_[static_cast<size_t>(k - 1)] && p >= mag_[static_cast<size_t>(k + 1)] && p > 0.01 * mx) {
                const double a = std::log(mag_[static_cast<size_t>(k - 1)] + 1e-30), b = std::log(p + 1e-30), d = std::log(mag_[static_cast<size_t>(k + 1)] + 1e-30);
                const double den = a - 2.0 * b + d, off = den < -1e-9 ? 0.5 * (a - d) / den : 0.0;
                const double f = (k + off) * fs_ / kFftN;
                const int pc = ((static_cast<int>(std::lround(12.0 * std::log2(f / 440.0))) + 9) % 12 + 12) % 12;
                c[static_cast<size_t>(pc)] += p;
            }
        }
    }
    int best = 0; double bv = 0.0; for (int i = 0; i < 12; ++i) if (c[static_cast<size_t>(i)] > bv) { bv = c[static_cast<size_t>(i)]; best = i; }
    frameValid_[static_cast<size_t>(framePos_)] = valid && bv > 0.0; frameStrongest_[static_cast<size_t>(framePos_)] = best; frameTime_[static_cast<size_t>(framePos_)] = t_ - kFftN / 2;
    framePos_ = (framePos_ + 1) % kFrames; frameCount_ = std::min(frameCount_ + 1, kFrames);
    // the chord of the last 4 s: the strongest classes (normalised per frame so that a loud moment does not decide alone)
    std::array<double, 12> sum{}; double tot = 0.0;
    for (int f = 0; f < kFrames; ++f) if (frameValid_[static_cast<size_t>(f)]) { double fs = 0; for (double x : chroma_[static_cast<size_t>(f)]) fs += x; if (fs > 0) for (int i = 0; i < 12; ++i) sum[static_cast<size_t>(i)] += chroma_[static_cast<size_t>(f)][static_cast<size_t>(i)] / fs; tot += 1.0; }
    chord_ = 0;
    if (tot > 0) {
        double m = 0; for (double x : sum) m = std::max(m, x);
        std::array<int, 12> order{}; for (int i = 0; i < 12; ++i) order[static_cast<size_t>(i)] = i;
        std::sort(order.begin(), order.end(), [&](int a, int b) { return sum[static_cast<size_t>(a)] > sum[static_cast<size_t>(b)]; });
        for (int n = 0; n < 4; ++n) if (sum[static_cast<size_t>(order[static_cast<size_t>(n)])] >= 0.5 * m && m > 0) chord_ |= 1 << order[static_cast<size_t>(n)];
    }
}

void Processor::spawn() {
    GrainState* g = nullptr; for (auto& x : grains_) if (!x.on) { g = &x; break; }
    if (!g) return;
    const int mode = static_cast<int>(target_[Mode] + 0.5);
    int len = std::max(32, static_cast<int>(std::lround(target_[Grain] * 0.001 * fs_)));
    double p = std::round(target_[Pitch]);
    const double dens = target_[Density];
    // the start (absolute sample index of the first sample read)
    double rate = std::exp2(p / 12.0), back = 0.0, start = 0.0; bool reverse = false;
    const double maxBack = 2.0 * fs_;
    auto chooseBack = [&](double r) {
        const double minBack = len * std::max(1.0, r) + 2.0;
        if (mode == Cloud) return minBack + target_[Spray] * 0.01 * rnd() * 0.5 * fs_;
        if (mode == Scatter) return minBack + rnd() * std::max(0.0, maxBack - minBack);
        const int k = 1 + static_cast<int>(rnd() * 4.0); return minBack + (k - 1) * len * std::max(1.0, r);
    };
    back = chooseBack(rate);
    start = static_cast<double>(t_) - back;
    // Harmony: the shift that brings the grain's source to a chord tone
    if (target_[Harmony] > 0.5 && chord_ != 0 && frameCount_ > 0) {
        int bf = -1; int64_t bd = INT64_MAX;   // the frame nearest to the middle of the grain's source
        for (int f = 0; f < kFrames; ++f) if (frameValid_[static_cast<size_t>(f)]) { const int64_t d = std::llabs(frameTime_[static_cast<size_t>(f)] - static_cast<int64_t>(start + 0.5 * len * rate)); if (d < bd) { bd = d; bf = f; } }
        if (bf >= 0) {
            const int s = frameStrongest_[static_cast<size_t>(bf)]; int cand[13], nc = 0;
            for (int k = static_cast<int>(p) - 6; k <= static_cast<int>(p) + 6; ++k) if (chord_ & (1 << (((s + k) % 12 + 12) % 12))) cand[nc++] = k;
            if (nc > 0) {
                std::sort(cand, cand + nc, [&](int a, int b) { return std::abs(a - p) < std::abs(b - p); });
                p = cand[(nc > 1 && rnd() < 0.35) ? 1 : 0];
                rate = std::exp2(p / 12.0); back = chooseBack(rate); start = static_cast<double>(t_) - back;
            }
        }
    }
    if (mode == Glitch) {
        if (rnd() < 0.33 && lastLen_ > 0.0) { start = lastStart_; len = static_cast<int>(lastLen_); }
        reverse = rnd() < 0.25;
        lastStart_ = start; lastLen_ = len;
    }
    g->on = true; g->age = 0; g->len = len;
    g->step = reverse ? -rate : rate; g->pos = reverse ? start + len * rate : start;
    g->g = 1.0 / std::sqrt(0.375 * std::max(1.0, dens * len / fs_));   // a Hann window has a mean square of 3/8
    const int spread = static_cast<int>(target_[Spread] + 0.5);
    const double pan = spread == 0 ? 0.0 : (rnd() * 2.0 - 1.0) * (spread == 1 ? 0.3 : 1.0), ang = (pan + 1.0) * kPi / 4.0;
    g->l = static_cast<float>(std::cos(ang)); g->r = static_cast<float>(std::sin(ang));
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const bool freeze = target_[Freeze] > 0.5;
    const double dens = target_[Density], interval = fs_ / dens;
    for (int i = 0; i < n; ++i) {
        const float in0 = ch[0][i], in1 = nch > 1 ? ch[1][i] : in0;
        if (!freeze) { buf_[0][wpos_] = in0; buf_[1][wpos_] = in1; wpos_ = (wpos_ + 1) & mask_; ++t_; if (++sinceChroma_ >= kChromaHop) { sinceChroma_ = 0; analyseChroma(); } }
        if (--nextIn_ <= 0.0) { spawn(); nextIn_ += interval * (0.7 + 0.6 * rnd()); }
        double l = 0.0, r = 0.0; int act = 0;
        for (auto& g : grains_) {
            if (!g.on) continue;
            ++act;
            const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * (g.age + 0.5) / g.len) , wg = w * g.g;
            const double pos = g.pos; const int64_t i0 = static_cast<int64_t>(std::floor(pos)); const double fr = pos - static_cast<double>(i0);
            // the frozen buffer ends at wpos_; t_ counts written samples: absolute index a maps to ring index a (mod size) while writing, and to (a - t_ + wpos_) once frozen at wpos_
            const auto at = [&](int c, int64_t a) { return static_cast<double>(buf_[static_cast<size_t>(c)][static_cast<size_t>(a) & mask_]); };
            const double s0 = at(0, i0) * (1.0 - fr) + at(0, i0 + 1) * fr, s1 = at(1, i0) * (1.0 - fr) + at(1, i0 + 1) * fr;
            const double m = 0.5 * (s0 + s1);
            l += wg * m * g.l * 1.4142; r += wg * m * g.r * 1.4142;
            g.pos += g.step; if (++g.age >= g.len) { g.on = false; }
        }
        active_ = act;
        for (int c = 0; c < nch; ++c) { double y = c == 0 ? l : r; if (!std::isfinite(y) || std::abs(y) < 1e-30) y = 0.0; ch[c][i] = static_cast<float>(y); }
    }
}

}  // namespace sw::cr03
