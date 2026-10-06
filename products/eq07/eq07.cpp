#include "eq07/eq07.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace sw::eq07 {

namespace {
const char* kIds[kPerBand] = {"on", "type", "freq", "gain", "q", "thresh", "range", "attack", "release"};
constexpr double kKneeDb = 6.0;                         // spec: 6 dB soft knee
constexpr double kShelfSplitHz = 1000.0;                // interpretation: Shelf / Cut act on the low side below 1 kHz
constexpr double kProminenceStartDb = 6.0, kProminenceFullDb = 12.0;  // spectral: design values
double softKneeOver(double over) {
    if (over <= -kKneeDb / 2) return 0.0;
    if (over < kKneeDb / 2) { const double k = over + kKneeDb / 2; return k * k / (2.0 * kKneeDb); }
    return over;
}
}

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        static std::vector<std::string> ids;
        ids.reserve(kBands * kPerBand);
        static const double defFreq[kBands] = {80, 180, 500, 1500, 4000, 10000};
        std::vector<ParamSpec> v;
        for (int b = 0; b < kBands; ++b) {
            for (int f = 0; f < kPerBand; ++f) ids.push_back("eq07.b" + std::to_string(b + 1) + "." + kIds[f]);
            const size_t i = static_cast<size_t>(b * kPerBand);
            v.push_back({ids[i + 0].c_str(), "On", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
            v.push_back({ids[i + 1].c_str(), "Type", 0, 3, 0, Curve::Step, 1, {0, 1, 2, 3}, "", {"Bell", "Shelf", "Cut", "Notch"}});
            v.push_back({ids[i + 2].c_str(), "Freq", 20, 20000, defFreq[b], Curve::Log, 1, {}, "Hz"});
            v.push_back({ids[i + 3].c_str(), "Gain", -24, 24, 0, Curve::Lin, 1, {}, "dB"});
            v.push_back({ids[i + 4].c_str(), "Q", 0.1, 20, 1, Curve::Log, 1, {}, ""});
            v.push_back({ids[i + 5].c_str(), "Threshold", -60, 0, -30, Curve::Lin, 1, {}, "dB"});
            v.push_back({ids[i + 6].c_str(), "Range", -24, 24, -6, Curve::Lin, 1, {}, "dB"});
            v.push_back({ids[i + 7].c_str(), "Attack", 0.1, 100, 10, Curve::Skew, 3, {}, "ms"});
            v.push_back({ids[i + 8].c_str(), "Release", 5, 2000, 120, Curve::Skew, 3, {}, "ms"});
        }
        v.push_back({"eq07.sc", "Sidechain", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Internal", "External"}});
        v.push_back({"eq07.spectral", "Spectral", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

BandShape Processor::shape(int b, double gainDb) const {
    BandShape s;
    const int ty = static_cast<int>(t(b, Type));
    const bool low = t(b, Freq) < kShelfSplitHz;
    s.type = ty == 0 ? BandShape::Bell : ty == 1 ? (low ? BandShape::LowShelf : BandShape::HighShelf)
           : ty == 2 ? (low ? BandShape::LowCut : BandShape::HighCut) : BandShape::Notch;
    s.freq = std::min(t(b, Freq), 0.49 * fs_);
    s.gainDb = gainDb;
    s.q = t(b, Q);
    s.slope = 12;
    return s;
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    spectral_ = target_[Spectral] > 0.5;
    for (auto& b : band_) { for (auto& f : b.f) f.reset(); for (auto& d : b.det) d.reset(); for (auto& l : b.lvl) { l.set(fs_, LevelDetector::Mode::Program); l.reset(); } b.amount = b.applied = 0; }
    win_.resize(kN);
    for (int n = 0; n < kN; ++n) win_[static_cast<size_t>(n)] = std::sqrt(0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * n / kN));  // sqrt-Hann (periodic)
    for (auto& s : st_) { s.in.assign(kN, 0.0); s.ola.assign(kN, 0.0); s.levelDb.assign(kN / 2 + 1, -200.0); s.w = s.r = s.hop = 0; }
    buf_.assign(kN, 0.0);
    gainDb_.assign(kN / 2 + 1, 0.0);
    prepared_ = true;
    snapToTargets();
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v));
}

void Processor::configure(int ramp) {
    for (int b = 0; b < kBands; ++b) {
        Band& bd = band_[static_cast<size_t>(b)];
        const double dyn = spectral_ ? 0.0 : t(b, Range) * bd.amount;  // spectral mode: dynamics live in the STFT
        const BandShape s = active(b) ? shape(b, hasGain(b) ? t(b, Gain) + dyn : 0.0) : BandShape{BandShape::Bell, 1000, 0, 1, 12};
        for (auto& f : bd.f) f.setup(s, fs_, ramp, false);
        for (auto& d : bd.det) d.setup(Svf::Mode::BandPass, std::min(t(b, Freq), 0.49 * fs_), fs_, t(b, Q), 0);
        bd.applied = bd.amount;
    }
}

void Processor::snapToTargets() { if (prepared_) configure(0); }

void Processor::spectralFrame(int c) {
    Stft& s = st_[static_cast<size_t>(c)];
    for (int n = 0; n < kN; ++n) buf_[static_cast<size_t>(n)] = s.in[static_cast<size_t>((s.w + n) % kN)] * win_[static_cast<size_t>(n)];
    fft_.forward(buf_);
    const int K = kN / 2;
    std::vector<double>& gain = gainDb_;  // dB per bin (preallocated: no allocation on the audio thread)
    std::fill(gain.begin(), gain.end(), 0.0);
    for (int b = 0; b < kBands; ++b) {
        const Band& bd = band_[static_cast<size_t>(b)];
        if (!active(b) || !hasGain(b) || std::abs(t(b, Range)) < 1e-6 || bd.amount <= 1e-6) continue;
        const double f0 = std::min(t(b, Freq), 0.49 * fs_), q = t(b, Q);
        const bool shelf = static_cast<int>(t(b, Type)) == 1, low = t(b, Freq) < kShelfSplitHz;
        auto weight = [&](double f) {  // the band's own region (band-pass, or the shelf's side)
            const double x = std::max(f, 1.0) / f0;
            if (!shelf) return 1.0 / std::sqrt(1.0 + q * q * (x - 1.0 / x) * (x - 1.0 / x));
            return low ? 1.0 / std::sqrt(1.0 + std::pow(x, 4.0)) : 1.0 / std::sqrt(1.0 + std::pow(1.0 / x, 4.0));
        };
        double wsum = 0, psum = 0;
        for (int k = 1; k < K; ++k) { const double w = weight(k * fs_ / kN); wsum += w; psum += w * std::pow(10.0, s.levelDb[static_cast<size_t>(k)] / 10.0); }
        const double mean = 10.0 * std::log10(std::max(psum / std::max(wsum, 1e-12), 1e-30));
        for (int k = 1; k < K; ++k) {
            const double w = weight(k * fs_ / kN);
            if (w < 0.05) continue;
            const double prom = std::clamp((s.levelDb[static_cast<size_t>(k)] - mean - kProminenceStartDb) / (kProminenceFullDb - kProminenceStartDb), 0.0, 1.0);
            gain[static_cast<size_t>(k)] += t(b, Range) * bd.amount * prom * w;
        }
    }
    // per-bin level for the next frame (attack/release in frames: fast up, slower down)
    for (int k = 0; k <= K; ++k) {
        const double lv = 20.0 * std::log10(std::abs(buf_[static_cast<size_t>(k)]) + 1e-12);
        double& L = s.levelDb[static_cast<size_t>(k)];
        L = lv > L ? lv : L + 0.3 * (lv - L);
        const double g = std::pow(10.0, gain[static_cast<size_t>(k)] / 20.0);
        buf_[static_cast<size_t>(k)] *= g;
        if (k > 0 && k < K) buf_[static_cast<size_t>(kN - k)] = std::conj(buf_[static_cast<size_t>(k)]);
    }
    fft_.inverse(buf_);
    for (int n = 0; n < kN; ++n) s.ola[static_cast<size_t>((s.r + n) % kN)] += 0.5 * buf_[static_cast<size_t>(n)].real() * win_[static_cast<size_t>(n)];  // Hann at 75 % overlap sums to 2
}

void Processor::run(float** ch, int numCh, int n, const float* const* sc, int scCh) {
    const int nch = std::min(numCh, 2);
    const bool ext = target_[Sidechain] > 0.5 && sc != nullptr && scCh > 0;
    for (int start = 0; start < n; start += kControl) {
        const int len = std::min(kControl, n - start);
        // detectors and ballistics (per sample), then one coefficient update per control block
        bool moved = false;
        for (int b = 0; b < kBands; ++b) {
            Band& bd = band_[static_cast<size_t>(b)];
            if (!active(b) || !hasGain(b) || std::abs(t(b, Range)) < 1e-6) { if (bd.amount != 0.0) { bd.amount = 0.0; moved = true; } continue; }
            const double atk = Ballistics::coef(fs_, t(b, Attack)), rel = Ballistics::coef(fs_, t(b, Release));
            const double range = std::abs(t(b, Range));
            for (int i = start; i < start + len; ++i) {
                double level = 0;
                for (int c = 0; c < nch; ++c) {
                    const double key = ext ? sc[std::min(c, scCh - 1)][i] : ch[c][i];
                    level = std::max(level, bd.lvl[static_cast<size_t>(c)].process(bd.det[static_cast<size_t>(c)].process(key)));
                }
                const double over = 20.0 * std::log10(std::max(level, 1e-9)) - t(b, Thresh);
                const double want = std::min(1.0, softKneeOver(over) / range);
                bd.amount = want + (want > bd.amount ? atk : rel) * (bd.amount - want);
            }
            if (std::abs(bd.amount - bd.applied) * range > 0.01) moved = true;
        }
        if (moved && !spectral_) configure(len);
        for (int i = start; i < start + len; ++i) {
            for (int c = 0; c < nch; ++c) {
                double y = ch[c][i];
                if (spectral_) {
                    Stft& s = st_[static_cast<size_t>(c)];
                    s.in[static_cast<size_t>(s.w)] = y;
                    s.w = (s.w + 1) % kN;
                    y = s.ola[static_cast<size_t>(s.r)];
                    s.ola[static_cast<size_t>(s.r)] = 0.0;
                    s.r = (s.r + 1) % kN;
                    if (++s.hop == kHop) { s.hop = 0; spectralFrame(c); }
                }
                for (int b = 0; b < kBands; ++b) if (active(b)) y = band_[static_cast<size_t>(b)].f[static_cast<size_t>(c)].process(y);
                ch[c][i] = static_cast<float>(std::abs(y) < 1e-30 ? 0.0 : y);
            }
        }
    }
}

}  // namespace sw::eq07
