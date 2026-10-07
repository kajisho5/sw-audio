#include "mt02/mt02.hpp"
#include <algorithm>
#include <cmath>

namespace sw::mt02 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"mt02.fft",    "FFT",       4096, 32768, 8192, Curve::Step, 1, {4096, 8192, 16384, 32768}, "", {"4k", "8k", "16k", "32k"}},
        {"mt02.speed",  "Speed",     0, 2, 1,    Curve::Step, 1, {0, 1, 2}, "", {"Slow", "Medium", "Fast"}},
        {"mt02.range",  "Range",     -120, -60, -90, Curve::Lin, 1, {}, "dB"},
        {"mt02.slope",  "Slope",     0, 6, 4.5,  Curve::Lin, 1, {}, "dB/oct"},
        {"mt02.smooth", "Smoothing", 0, 4, 3,    Curve::Step, 1, {0, 1, 2, 3, 4}, "", {"Off", "1/24 oct", "1/12 oct", "1/6 oct", "1/3 oct"}},
        {"mt02.display","Display",   0, 2, 1,    Curve::Step, 1, {0, 1, 2}, "", {"Peak", "Average", "Hold"}},
    };
    return s;
}

namespace {
constexpr double kPi = 3.14159265358979323846, kTau[3] = {2.0, 0.5, 0.12}, kPeakFallDbPerS = 20.0, kOctFrac[5] = {0.0, 1.0 / 24, 1.0 / 12, 1.0 / 6, 1.0 / 3};
int sizeIndex(double v) { return v < 6000 ? 0 : (v < 12000 ? 1 : (v < 24000 ? 2 : 3)); }
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; n_ = 8192; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (int i = 0; i < 4; ++i) fft_[static_cast<size_t>(i)] = std::make_unique<Fft>(4096 << i);
    ring_.assign(kMaxFft, 0.0f); work_.assign(kMaxFft, {0.0, 0.0}); win_.assign(kMaxFft, 0.0);
    const size_t nb = kMaxFft / 2 + 1;
    avg_.assign(nb, 0.0); peak_.assign(nb, 0.0); hold_.assign(nb, 0.0); ref_.assign(nb, 0.0); tmp_.assign(nb, 0.0); prefix_.assign(nb + 1, 0.0);
    n_ = 4096 << sizeIndex(target_[FftSize]); hop_ = n_ / 4; nCur_ = n_;
    for (int i = 0; i < n_; ++i) win_[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n_);
    wpos_ = 0; filled_ = 0; since_ = 0; haveRef_ = false; prepared_ = true;
}

void Processor::reset() { std::fill(avg_.begin(), avg_.end(), 0.0); std::fill(peak_.begin(), peak_.end(), 0.0); std::fill(hold_.begin(), hold_.end(), 0.0); filled_ = 0; since_ = 0; }

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == FftSize && prepared_) {
        n_ = 4096 << sizeIndex(v); hop_ = n_ / 4; nCur_ = n_;
        for (int i = 0; i < n_; ++i) win_[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n_);
        reset(); haveRef_ = false;
    }
}

void Processor::frame() {
    const int idx = sizeIndex(n_);
    for (int i = 0; i < n_; ++i) work_[static_cast<size_t>(i)] = {win_[static_cast<size_t>(i)] * ring_[static_cast<size_t>((wpos_ + i) % n_)], 0.0};
    // the ring has n_ valid samples when filled_ >= n_: oldest at wpos_
    fft_[static_cast<size_t>(idx)]->forward(work_);
    const double h = static_cast<double>(hop_) / fs_, a = 1.0 - std::exp(-h / kTau[std::clamp(static_cast<int>(target_[Speed] + 0.5), 0, 2)]), fall = std::pow(10.0, -kPeakFallDbPerS * h / 10.0), sc = 4.0 / n_;
    const int nb = n_ / 2 + 1;
    for (int k = 0; k < nb; ++k) {
        const double p = std::norm(work_[static_cast<size_t>(k)]) * sc * sc;
        const size_t i = static_cast<size_t>(k);
        avg_[i] = avg_[i] <= 0.0 ? p : avg_[i] + a * (p - avg_[i]);
        peak_[i] = std::max(p, peak_[i] * fall);
        hold_[i] = std::max(p, hold_[i]);
    }
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    for (int i = 0; i < n; ++i) {
        const double m = nch > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i];
        ring_[static_cast<size_t>(wpos_)] = static_cast<float>(m); wpos_ = (wpos_ + 1) % n_;
        if (filled_ < n_) ++filled_;
        if (++since_ >= hop_) { since_ = 0; if (filled_ >= n_) frame(); }
    }
}

void Processor::captureReference() { ref_ = avg_; haveRef_ = true; }

void Processor::spectrumDb(std::vector<float>& out) const {
    const int nb = n_ / 2 + 1; out.resize(static_cast<size_t>(nb));
    const int disp = static_cast<int>(target_[Display] + 0.5);
    const std::vector<double>& src = disp == Peak ? peak_ : (disp == Hold ? hold_ : avg_);
    const double oct = kOctFrac[std::clamp(static_cast<int>(target_[Smoothing] + 0.5), 0, 4)], floorDb = target_[Range], slope = target_[Slope];
    prefix_[0] = 0.0; for (int k = 0; k < nb; ++k) prefix_[static_cast<size_t>(k + 1)] = prefix_[static_cast<size_t>(k)] + src[static_cast<size_t>(k)];
    for (int k = 0; k < nb; ++k) {
        double p = src[static_cast<size_t>(k)];
        if (oct > 0.0 && k > 0) {
            const double f = std::exp2(oct * 0.5);
            const int lo = std::max(0, static_cast<int>(std::floor(k / f))), hi = std::min(nb - 1, std::max(k, static_cast<int>(std::ceil(k * f))));
            p = (prefix_[static_cast<size_t>(hi + 1)] - prefix_[static_cast<size_t>(lo)]) / static_cast<double>(hi - lo + 1);
        }
        double db = p > 1e-30 ? 10.0 * std::log10(p) : -300.0;
        if (k > 0) db += slope * std::log2(binHz(k) / 1000.0);
        out[static_cast<size_t>(k)] = static_cast<float>(std::max(db, floorDb));
    }
}

void Processor::compareDb(std::vector<float>& out) const {
    const int nb = n_ / 2 + 1; out.assign(static_cast<size_t>(nb), 0.0f);
    if (!haveRef_) return;
    for (int k = 0; k < nb; ++k) { const double a = avg_[static_cast<size_t>(k)], r = ref_[static_cast<size_t>(k)]; if (a > 1e-30 && r > 1e-30) out[static_cast<size_t>(k)] = static_cast<float>(10.0 * std::log10(a / r)); }
}

}  // namespace sw::mt02
