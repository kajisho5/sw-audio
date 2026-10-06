#include "ms03/ms03.hpp"
#include <algorithm>
#include <cmath>

namespace sw::ms03 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v;
        static std::vector<std::string> names;
        names.clear(); names.reserve(64);
        auto keep = [&](const std::string& t) { names.push_back(t); return names.back().c_str(); };
        const char* kn[] = {"gain", "ceiling", "release"};
        const char* disp[] = {"Gain", "Ceiling", "Release"};
        for (int n = 1; n <= 4; ++n)
            for (int k = 0; k < 3; ++k) {
                const std::string id = "ms03.b" + std::to_string(n) + "." + kn[k], nm = "Band " + std::to_string(n) + " " + disp[k];
                if (k == BGain) v.push_back({keep(id), keep(nm), 0, 12, 0, Curve::Lin, 1, {}, "dB"});
                else if (k == BCeiling) v.push_back({keep(id), keep(nm), -12, 0, 0, Curve::Lin, 1, {}, "dB"});
                else v.push_back({keep(id), keep(nm), 1, 1000, 60, Curve::Skew, 3, {}, "ms"});
            }
        v.push_back({"ms03.x1.freq", "Crossover 1", 20, 20000, 120,  Curve::Log, 1, {}, "Hz"});
        v.push_back({"ms03.x2.freq", "Crossover 2", 20, 20000, 1000, Curve::Log, 1, {}, "Hz"});
        v.push_back({"ms03.x3.freq", "Crossover 3", 20, 20000, 6000, Curve::Log, 1, {}, "Hz"});
        v.push_back({"ms03.outceiling", "Out ceiling", -12, 0, -1, Curve::Lin, 1, {}, "dBTP"});
        v.push_back({"ms03.char", "Character", 0, 2, 1, Curve::Step, 1, {0, 1, 2}, "", {"Clean", "Punch", "Dense"}});
        v.push_back({"ms03.evo.on", "Link bands", 0, 1, 1, Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
        return v;
    }();
    return s;
}

namespace {
constexpr double kBandLookaheadMs = 2.0, kLinkLookaheadMs = 1.0, kFinalLookaheadMs = 0.5;
constexpr double kReleaseScale[3] = {1.0, 0.5, 2.0};   // Character: Clean / Punch / Dense (design)
constexpr double kDenseKnee = 1.25;                    // Dense: soft pre-clip at 1.25 x the band ceiling
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

int Processor::latencySamples() const {
    const int a = std::max(1, static_cast<int>(std::lround(kBandLookaheadMs * 0.001 * fs_))), b = std::max(1, static_cast<int>(std::lround(kLinkLookaheadMs * 0.001 * fs_)));
    const int c = std::max(1, static_cast<int>(std::lround(kFinalLookaheadMs * 0.001 * fs_)));
    return a + b + c + TruePeakDetector::kTapsPerPhase;
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    la1_ = std::max(1, static_cast<int>(std::lround(kBandLookaheadMs * 0.001 * fs_)));
    la2_ = std::max(1, static_cast<int>(std::lround(kLinkLookaheadMs * 0.001 * fs_)));
    la3_ = std::max(1, static_cast<int>(std::lround(kFinalLookaheadMs * 0.001 * fs_)));
    for (auto& b : bandLim_) b.prepare(fs_, 2, la1_, false, 4);
    linkLim_.prepare(fs_, 2, la2_, false, 4);
    finalLim_.prepare(fs_, 2, la3_, true, 4);
    for (size_t b = 0; b < 4; ++b) {
        gain_[b].reset(fs_, 20.0, std::pow(10.0, target_[static_cast<size_t>(band(static_cast<int>(b), BGain))] / 20.0));
        for (size_t c = 0; c < 2; ++c) { band_[b][c].assign(kChunk, 0.0f); delayed_[b][c].assign(static_cast<size_t>(la2_), 0.0f); }
    }
    for (size_t c = 0; c < 2; ++c) { sum_[c].assign(kChunk, 0.0f); sumLimited_[c].assign(kChunk, 0.0f); }
    energy_ = {0, 0, 0, 0}; energyC_ = std::exp(-1.0 / (0.050 * fs_)); gPrev_ = 1.0; dpos_ = 0;
    applyCrossovers(); applyBands();
}

void Processor::applyCrossovers() {
    const auto f = Lr4Split4::effective(target_[X1], target_[X2], target_[X3], fs_);
    split_.setup(f[0], f[1], f[2], fs_);
}

void Processor::applyBands() {
    const double mult = kReleaseScale[static_cast<int>(target_[Char] + 0.5)];
    for (int b = 0; b < 4; ++b)
        bandLim_[static_cast<size_t>(b)].set(target_[static_cast<size_t>(band(b, BCeiling))] - 0.02, std::max(1.0, target_[static_cast<size_t>(band(b, BRelease))] * mult), 1.0);
    linkLim_.set(target_[OutCeiling] - 0.02, 80.0, 1.0);
    finalLim_.set(target_[OutCeiling] - 0.02, 50.0, 1.0);
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id >= X1 && id <= X3) applyCrossovers();
    else if (id == OutCeiling || id == Char) applyBands();
    else if (id < 12) {
        const int b = id / 3;
        if (id % 3 == BGain) gain_[static_cast<size_t>(b)].setTarget(std::pow(10.0, v / 20.0));
        else applyBands();
    }
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    for (int off = 0; off < n; off += kChunk) {
        float* p[2] = {ch[0] + off, nch > 1 ? ch[1] + off : ch[0] + off};
        runChunk(p, nch, std::min(kChunk, n - off));
    }
}

void Processor::runChunk(float** ch, int nch, int n) {
    const bool dense = static_cast<int>(target_[Char] + 0.5) == 2, link = target_[Link] > 0.5;
    // 1) split, band gain (+ soft pre-clip for Dense)
    for (int i = 0; i < n; ++i) {
        double g[4];
        for (int b = 0; b < 4; ++b) g[b] = gain_[static_cast<size_t>(b)].next();
        for (int c = 0; c < nch; ++c) {
            double out[4]; split_.process(c, ch[c][i], out);
            for (int b = 0; b < 4; ++b) {
                double v = out[b] * g[b];
                if (dense) { const double cl = kDenseKnee * std::pow(10.0, target_[static_cast<size_t>(band(b, BCeiling))] / 20.0); v = cl * std::tanh(v / cl); }
                band_[static_cast<size_t>(b)][static_cast<size_t>(c)][static_cast<size_t>(i)] = static_cast<float>(v);
            }
        }
    }
    // 2) band limiters (look-ahead la1_)
    for (int b = 0; b < 4; ++b) { float* bp[2] = {band_[static_cast<size_t>(b)][0].data(), band_[static_cast<size_t>(b)][nch > 1 ? 1 : 0].data()}; bandLim_[static_cast<size_t>(b)].process(bp, nch, n); }
    // 3) sum -> link stage limiter; bands delayed by la2_ in step
    for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) { double s = 0; for (int b = 0; b < 4; ++b) s += band_[static_cast<size_t>(b)][static_cast<size_t>(c)][static_cast<size_t>(i)]; sum_[static_cast<size_t>(c)][static_cast<size_t>(i)] = static_cast<float>(s); sumLimited_[static_cast<size_t>(c)][static_cast<size_t>(i)] = static_cast<float>(s); }
    { float* sp[2] = {sumLimited_[0].data(), sumLimited_[nch > 1 ? 1 : 0].data()}; linkLim_.process(sp, nch, n); }
    // 4) delayed bands, shared-out reduction, final true-peak limiter
    for (int i = 0; i < n; ++i) {
        const size_t pos = static_cast<size_t>(dpos_);
        double db[4][2] = {}, ds[2] = {0, 0}, e[4] = {0, 0, 0, 0};   // e: peak of the band over both channels
        for (int b = 0; b < 4; ++b)
            for (int c = 0; c < nch; ++c) {
                auto& ring = delayed_[static_cast<size_t>(b)][static_cast<size_t>(c)];
                db[b][c] = ring[pos]; ring[pos] = band_[static_cast<size_t>(b)][static_cast<size_t>(c)][static_cast<size_t>(i)];
                ds[c] += db[b][c]; e[b] = std::max(e[b], std::abs(db[b][c]));
            }
        dpos_ = (dpos_ + 1) % la2_;
        double g = 1.0;   // gain the link stage applied to the (delayed) sum
        for (int c = 0; c < nch; ++c) { const double a = std::abs(ds[c]); if (a > 1e-6) g = std::min(g, std::abs(sumLimited_[static_cast<size_t>(c)][static_cast<size_t>(i)]) / a); }
        if (g > 1.0) g = 1.0;
        double tot = 0, sq = 0;
        for (int b = 0; b < 4; ++b) { energy_[static_cast<size_t>(b)] = std::max(e[b], energyC_ * energy_[static_cast<size_t>(b)]); sq += energy_[static_cast<size_t>(b)] * energy_[static_cast<size_t>(b)]; tot   /* instant attack, 50 ms fall (longer than a bass cycle): whoever has been loud lately takes the blame */ += energy_[static_cast<size_t>(b)]; }
        for (int c = 0; c < nch; ++c) {
            double y;
            if (link && g < 0.9999 && tot > 1e-12) {
                y = 0;
                for (int b = 0; b < 4; ++b) y += std::pow(g, energy_[static_cast<size_t>(b)] * tot / sq) * db[b][c];   // exponent e_b * sum(e) / sum(e^2): the loudest band takes the most; equal bands = the plain gain
            } else {
                y = sumLimited_[static_cast<size_t>(c)][static_cast<size_t>(i)];
            }
            if (!std::isfinite(y)) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
    finalLim_.process(ch, nch, n);
    for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) if (std::abs(ch[c][i]) < 1e-30f) ch[c][i] = 0.0f;
}

}  // namespace sw::ms03
