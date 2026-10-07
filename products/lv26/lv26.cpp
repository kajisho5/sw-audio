#include "lv26/lv26.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv26 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"lv26.width",    "Width",           0, 200, 100, Curve::Lin, 1, {}, "%"},
            {"lv26.lowmono",  "Low mono",        20, 300, 120, Curve::Log, 1, {}, "Hz"},
            {"lv26.monocheck", "Mono check",     0, 1, 0,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"lv26.autofix",  "Auto phase fix",  0, 1, 1,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        };
        v[LowMono].minLabel = "Off"; v[MonoCheck].automatable = false;
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; g_.fill(1.0); }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate; splitLR_.setup(250.0, 1500.0, 6000.0, fs_); lowMono_.setup(Svf::Mode::HighPass, target_[LowMono], fs_, 0.70710678, 0);
    pLR_.fill(0); pLL_.fill(0); pRR_.fill(0); corr_.fill(0); g_.fill(1.0); appliedDb_.fill(0.0); tick_ = 0; setSideFilters(); for (auto& f : sideF_) f.reset(); prepared_ = true;
}
void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v));
    if (id == LowMono && prepared_) lowMono_.setup(Svf::Mode::HighPass, target_[LowMono], fs_, 0.70710678, 0);
}

void Processor::setSideFilters() {
    // the four bands as a cascade of a low shelf (250 Hz), two bells (700 Hz, 3 kHz: the middle of the two bands between the crossovers) and a high shelf (6 kHz): at 0 dB each is exactly the identity
    auto db = [&](size_t k) { return 20.0 * std::log10(std::max(g_[k], 1e-9)); };
    sideF_[0].setup(Svf::Mode::LowShelf, 250.0, fs_, 0.70710678, db(0)); sideF_[1].setup(Svf::Mode::Bell, 600.0, fs_, 0.7, db(1));
    sideF_[2].setup(Svf::Mode::Bell, 3000.0, fs_, 0.7, db(2)); sideF_[3].setup(Svf::Mode::HighShelf, std::min(6000.0, fs_ * 0.45), fs_, 0.70710678, db(3));
    for (size_t k = 0; k < 4; ++k) appliedDb_[k] = db(k);
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 2 || n <= 0) return;   // mono input: nothing to do (no side)
    const double w = target_[Width] * 0.01; const bool lowOn = target_[LowMono] > 20.0 * 1.0001, fix = target_[AutoPhaseFix] > 0.5, check = target_[MonoCheck] > 0.5;
    const double ac = std::exp(-1.0 / (0.2 * fs_)), ag = 1.0 - std::exp(-1.0 / (0.1 * fs_));
    for (int i = 0; i < n; ++i) {
        const double l = ch[0][i], r = ch[1][i], m = 0.5 * (l + r); double s = 0.5 * (l - r);
        if (fix) {
            double bl[4], br[4];
            splitLR_.process(0, l, bl); splitLR_.process(1, r, br);
            bool moved = false;
            for (int b = 0; b < kBands; ++b) {
                const size_t k = static_cast<size_t>(b);
                pLR_[k] = ac * pLR_[k] + (1 - ac) * bl[b] * br[b]; pLL_[k] = ac * pLL_[k] + (1 - ac) * bl[b] * bl[b]; pRR_[k] = ac * pRR_[k] + (1 - ac) * br[b] * br[b];
                const double d = std::sqrt(pLL_[k] * pRR_[k]); corr_[k] = d > 1e-12 ? pLR_[k] / d : 0.0;
                const double red = std::clamp((-corr_[k] - 0.2) / 0.8, 0.0, 1.0), tg = std::pow(10.0, -12.0 * red / 20.0);
                g_[k] += ag * (tg - g_[k]); if (std::abs(g_[k] - 1.0) < 1e-9) g_[k] = 1.0;
                if (std::abs(20.0 * std::log10(g_[k]) - appliedDb_[k]) > 0.02) moved = true;
            }
            if (moved && (++tick_ & 15) == 0) setSideFilters();
            for (auto& f : sideF_) s = f.process(s);
        }
        s *= w;
        if (lowOn) s = lowMono_.process(s);
        double ol = m + s, orr = m - s;
        if (check) ol = orr = m;
        const float fl = static_cast<float>(ol), fr = static_cast<float>(orr);
        ch[0][i] = std::abs(fl) < 1e-30f ? 0.0f : fl; ch[1][i] = std::abs(fr) < 1e-30f ? 0.0f : fr;
    }
}

}  // namespace sw::lv26
