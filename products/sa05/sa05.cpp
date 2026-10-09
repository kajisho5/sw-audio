#include "sa05/sa05.hpp"
#include <algorithm>
#include <cmath>

namespace sw::sa05 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"sa05.tune",      "Tune",      1000, 16000, 4500, Curve::Log, 1, {}, "Hz"},
        {"sa05.harmonics", "Harmonics", 0, 100, 35,    Curve::Lin,  1, {}, "%"},
        {"sa05.mix",       "Mix",       0, 100, 25,    Curve::Lin,  1, {}, "%"},
        {"sa05.lowdrive",  "Low drive", 0, 100, 0,     Curve::Lin,  1, {}, "%"},
        {"sa05.mode",      "Mode",      0, 2, 0,       Curve::Step, 1, {0, 1, 2}, "", {"Even", "Odd", "Both"}},
        {"sa05.monolow",   "Mono low",  0, 1, 0,       Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"sa05.evo.on",    "Auto fill", 0, 1, 1,       Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        oversampleSpec("sa05.os"),
    };
    return s;
}

namespace { constexpr double kEvenGain = 1.0, kOddGain = 1.4, kLowScale = 0.5, kSlopeDbPerOct = 1.5, kMaxFillDb = 12.0; }

// one sample in (band-limited), harmonics out; mode 0 even, 1 odd, 2 both. The envelope is the band's mean square (10 ms)
double Processor::Gen::process(double x, int mode, double msC) {
    ms = x * x + msC * (ms - x * x);
    const double r = std::sqrt(ms) + 1e-6, m2 = ms + 1e-10;
    const double s = ms;
    return os.process(x, [&](double u) {
        double h = 0;
        if (mode != 1) h += kEvenGain * (u * u - s) / r;
        if (mode != 0) h += kOddGain * (u * u * u / m2 - 1.5 * u);
        return h;
    });
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    msC_ = std::exp(-1.0 / (0.010 * fs_));
    for (auto& g : genHi_) g = Gen{}; for (auto& g : genLo_) g = Gen{};
    applyOversample();
    an_.setup(fs_, 4096, 1.0); sinceAnalysis_ = 0;
    fill_ = {0, 0, 0}; fillWant_ = {0, 0, 0};
    fillC_ = 1.0 - std::exp(-0.1 / 1.0);   // 1 s follow, updated every 100 ms
    updateFilters();
}

void Processor::updateFilters() {
    const double t = std::min(target_[Tune], fs_ * 0.45);
    for (size_t c = 0; c < 2; ++c) {
        hp_[c].setup(Svf::Mode::HighPass, t, fs_); post_[c].setup(Svf::Mode::HighPass, t, fs_);
        for (auto& f : lows_[c]) f.setup(Svf::Mode::LowPass, 200.0, fs_, 0.70710678, 0);
        lowHp_[c].setup(Svf::Mode::HighPass, 120.0, fs_, 0.70710678, 0);
        fillEq_[c].r1.setup(Svf::Mode::Bell, std::min(1.4 * t, 0.45 * fs_), fs_, 1.0, fill_[0]);
        fillEq_[c].r2.setup(Svf::Mode::Bell, std::min(2.8 * t, 0.45 * fs_), fs_, 1.0, fill_[1]);
        fillEq_[c].r3.setup(Svf::Mode::HighShelf, std::min(5.0 * t, 0.45 * fs_), fs_, 0.70710678, fill_[2]);
    }
}

void Processor::applyOversample() {
    const int f = static_cast<int>(target_[Oversample]);
    for (auto& g : genHi_) g.os.setFactor(f);
    for (auto& g : genLo_) g.os.setFactor(f);
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Tune) updateFilters();
    else if (id == Oversample) applyOversample();
}

// 1/3-octave levels above Tune against a target that falls 1.5 dB/oct from the average of the bands just below Tune
void Processor::analyse() {
    const double tune = target_[Tune];
    double anchor = 0; int na = 0;
    for (int b = 0; b < ThirdOctaveAnalyzer::kBands; ++b) { const double fc = ThirdOctaveAnalyzer::centerHz(b); if (fc >= tune / 8.0 && fc < tune / 1.2 && fc >= 200.0) { anchor += std::max(an_.levelDb(b), -100.0); ++na; } }
    double sum[3] = {0, 0, 0}; int cnt[3] = {0, 0, 0};
    if (na > 0 && an_.frames() > 2) {
        anchor /= na;
        for (int b = 0; b < ThirdOctaveAnalyzer::kBands; ++b) {
            const double fc = ThirdOctaveAnalyzer::centerHz(b);
            if (fc < tune || fc > 20000.0 || fc > 0.45 * fs_) continue;
            const double target = anchor - kSlopeDbPerOct * std::log2(fc / (tune / 1.2));
            const double def = target - std::max(an_.levelDb(b), -100.0);
            const int r = fc < 2.0 * tune ? 0 : fc < 4.0 * tune ? 1 : 2;
            sum[r] += def; ++cnt[r];
        }
    }
    for (int r = 0; r < 3; ++r) fillWant_[static_cast<size_t>(r)] = cnt[r] > 0 ? std::clamp(sum[r] / cnt[r], 0.0, kMaxFillDb) : 0.0;
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const int mode = static_cast<int>(target_[Mode] + 0.5);
    const double amount = target_[Harmonics] / 100.0, lowAmt = kLowScale * target_[LowDrive] / 100.0;
    const bool autoFill = target_[AutoFill] > 0.5, monoLow = target_[MonoLow] > 0.5 && nch > 1;
    // Auto fill analysis on the mono sum of the input, every 100 ms of audio
    if (autoFill) {
        for (int i = 0; i < n; ++i) an_.push(nch > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i]);
        sinceAnalysis_ += n;
        if (sinceAnalysis_ >= static_cast<int>(0.1 * fs_)) { sinceAnalysis_ = 0; analyse(); for (size_t r = 0; r < 3; ++r) fill_[r] += (fillWant_[r] - fill_[r]) * fillC_; updateFilters(); }
    }
    for (int i = 0; i < n; ++i) {
        double x[2] = {0, 0}, harm[2] = {0, 0}, lowH[2] = {0, 0};
        for (int c = 0; c < nch; ++c) x[c] = ch[c][i];
        for (int c = 0; c < nch; ++c) {
            const size_t cc = static_cast<size_t>(c);
            const double band = hp_[cc].process(x[c]);
            double h = genHi_[cc].process(band, mode, msC_);
            harm[c] = post_[cc].process(h);   // keep only what lies above Tune (intermodulation and DC fall away)
        }
        if (lowAmt > 0.0) {
            if (monoLow) {
                const double m = 0.5 * (x[0] + x[1]);
                const double lo = lows_[0][1].process(lows_[0][0].process(m));
                const double h = lowHp_[0].process(genLo_[0].process(lo, mode, msC_)) * lowAmt;
                lowH[0] = lowH[1] = h;
            } else {
                for (int c = 0; c < nch; ++c) { const size_t cc = static_cast<size_t>(c); const double lo = lows_[cc][1].process(lows_[cc][0].process(x[c])); lowH[c] = lowHp_[cc].process(genLo_[cc].process(lo, mode, msC_)) * lowAmt; }
            }
        }
        for (int c = 0; c < nch; ++c) {
            const size_t cc = static_cast<size_t>(c);
            double h = harm[c];
            if (autoFill) h = fillEq_[cc].r3.process(fillEq_[cc].r2.process(fillEq_[cc].r1.process(h)));
            double y = x[c] + amount * h + lowH[c];
            if (!std::isfinite(y)) y = 0.0;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::sa05
