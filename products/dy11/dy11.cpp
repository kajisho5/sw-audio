#include "dy11/dy11.hpp"
#include <algorithm>
#include <cmath>

namespace sw::dy11 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v;
        static std::vector<std::string> names;
        names.clear(); names.reserve(128);
        auto keep = [&](const std::string& t) { names.push_back(t); return names.back().c_str(); };
        const char* kn[] = {"mode", "freq", "thresh", "ratio", "attack", "release", "range", "gain", "width"};
        const char* disp[] = {"Mode", "Freq", "Threshold", "Ratio", "Attack", "Release", "Range", "Gain", "Width"};
        const double freqs[] = {60, 200, 600, 2000, 6000, 14000};
        for (int n = 1; n <= kBands; ++n)
            for (int k = 0; k < kPerBand; ++k) {
                const std::string id = "dy11.b" + std::to_string(n) + "." + kn[k], nm = "Band " + std::to_string(n) + " " + disp[k];
                switch (k) {
                    case BMode:    v.push_back({keep(id), keep(nm), 0, 2, 0, Curve::Step, 1, {0, 1, 2}, "", {"Compress", "Expand", "Dynamic EQ"}}); break;
                    case BFreq:    v.push_back({keep(id), keep(nm), 20, 20000, freqs[n - 1], Curve::Log, 1, {}, "Hz"}); break;
                    case BThresh:  v.push_back({keep(id), keep(nm), -60, 0, 0,    Curve::Lin, 1, {}, "dB"}); break;
                    case BRatio:   v.push_back({keep(id), keep(nm), 1, 20, 2,     Curve::Log, 1, {}, ":1"}); break;
                    case BAttack:  v.push_back({keep(id), keep(nm), 0.1, 200, 20, Curve::Skew, 3, {}, "ms"}); break;
                    case BRelease: v.push_back({keep(id), keep(nm), 5, 3000, 100, Curve::Skew, 3, {}, "ms"}); break;
                    case BRange:   v.push_back({keep(id), keep(nm), -24, 0, -12,  Curve::Lin, 1, {}, "dB"}); break;
                    case BGain:    v.push_back({keep(id), keep(nm), -12, 12, 0,   Curve::Lin, 1, {}, "dB"}); break;
                    default:       v.push_back({keep(id), keep(nm), 0.1, 4, 1.0,  Curve::Log, 1, {}, "oct"}); break;
                }
            }
        v.push_back({"dy11.out", "Output", -24, 24, 0, Curve::Lin, 1, {}, "dB"});
        return v;
    }();
    return s;
}

namespace {
double qFromOctaves(double bw) { return 1.0 / (2.0 * std::sinh(std::log(2.0) / 2.0 * std::max(bw, 0.05))); }  // analog bell: bandwidth (oct) -> Q
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& b : band_) { for (auto& f : b.f) f.reset(); for (auto& d : b.det) d.reset(); for (auto& l : b.lvl) { l.set(fs_, LevelDetector::Mode::Rms); l.reset(); } b.gr = 0; b.applied = 1e9; }
    prepared_ = true;
    configure(0);
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v));
}

Processor::Region Processor::region(int b, double gainDb) const {
    const double f = std::min(t(b, BFreq), 0.49 * fs_);
    const double fLo = b > 0 ? std::min(t(b - 1, BFreq), f) : f, fHi = b < kBands - 1 ? std::max(t(b + 1, BFreq), f) : f;
    if (mode(b) == 2) {   // Dynamic EQ: a bell of Width octaves
        const double q = qFromOctaves(t(b, BWidth));
        return {BandShape{BandShape::Bell, f, gainDb, q, 12}, Svf::Mode::BandPass, f, q};
    }
    if (b == 0) {         // low shelf up to the midpoint with band 2
        const double mid = std::min(std::sqrt(f * fHi), 0.49 * fs_);
        return {BandShape{BandShape::LowShelf, mid, gainDb, 0.7071, 12}, Svf::Mode::LowPass, mid, 0.7071};
    }
    if (b == kBands - 1) {
        const double mid = std::min(std::sqrt(f * fLo), 0.49 * fs_);
        return {BandShape{BandShape::HighShelf, mid, gainDb, 0.7071, 12}, Svf::Mode::HighPass, mid, 0.7071};
    }
    const double lo = std::sqrt(f * fLo), hi = std::min(std::sqrt(f * fHi), 0.49 * fs_);
    const double q = qFromOctaves(std::log2(std::max(hi / std::max(lo, 1.0), 1.05)));
    return {BandShape{BandShape::Bell, f, gainDb, q, 12}, Svf::Mode::BandPass, f, q};
}

void Processor::configure(int ramp) {
    for (int b = 0; b < kBands; ++b) {
        Band& bd = band_[static_cast<size_t>(b)];
        const Region r = region(b, t(b, BGain) + bd.gr);
        for (auto& f : bd.f) f.setup(r.shape, fs_, ramp, false);
        for (auto& d : bd.det) d.setup(r.detMode, r.detFreq, fs_, r.detQ, 0);
        bd.applied = bd.gr;
    }
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const double out = std::pow(10.0, target_[Output] / 20.0);
    for (int start = 0; start < n; start += kControl) {
        const int len = std::min(kControl, n - start);
        bool moved = false;
        for (int b = 0; b < kBands; ++b) {
            Band& bd = band_[static_cast<size_t>(b)];
            const double atk = Ballistics::coef(fs_, t(b, BAttack)), rel = Ballistics::coef(fs_, t(b, BRelease));
            const double range = t(b, BRange), thr = t(b, BThresh), ratio = t(b, BRatio);
            const int m = mode(b);
            for (int i = start; i < start + len; ++i) {
                double level = 0;
                for (int c = 0; c < nch; ++c) level = std::max(level, bd.lvl[static_cast<size_t>(c)].process(bd.det[static_cast<size_t>(c)].process(ch[c][i])));
                const double lv = 20.0 * std::log10(std::max(level, 1e-9));
                double want = 0.0;
                if (m == 1) want = lv < thr ? -(thr - lv) * (ratio - 1.0) : 0.0;                 // Expand: below the threshold, (ratio - 1) dB per dB
                else { const double over = lv - thr; want = over <= -3.0 ? 0.0 : over >= 3.0 ? -over * (1.0 - 1.0 / ratio) : -(1.0 - 1.0 / ratio) * (over + 3.0) * (over + 3.0) / 12.0; }   // 6 dB soft knee
                want = std::clamp(want, range, 0.0);
                bd.gr = want + (want < bd.gr ? atk : rel) * (bd.gr - want);
            }
            if (std::abs(bd.gr - bd.applied) > 0.01) moved = true;
        }
        if (moved) configure(len);
        for (int i = start; i < start + len; ++i)
            for (int c = 0; c < nch; ++c) {
                double y = ch[c][i];
                for (int b = 0; b < kBands; ++b) y = band_[static_cast<size_t>(b)].f[static_cast<size_t>(c)].process(y);
                y *= out;
                if (!std::isfinite(y)) y = 0.0;
                if (std::abs(y) < 1e-30) y = 0.0;
                ch[c][i] = static_cast<float>(y);
            }
    }
}

}  // namespace sw::dy11
