#include "mt03/mt03.hpp"
#include <algorithm>
#include <cmath>

namespace sw::mt03 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"mt03.scale",    "Scale",     0, 2, 1,    Curve::Step, 1, {0, 1, 2}, "", {"Linear", "Log", "Mel"}},
        {"mt03.scroll",   "Scroll",    2, 60, 10,  Curve::Log, 1, {}, "s"},
        {"mt03.floor",    "Floor",     -120, -60, -90, Curve::Lin, 1, {}, "dB"},
        {"mt03.contrast", "Contrast",  0, 2, 1,    Curve::Step, 1, {0, 1, 2}, "", {"Low", "Mid", "High"}},
        {"mt03.palette",  "Palette",   0, 1, 0,    Curve::Step, 1, {0, 1}, "", {"Mono", "Heat"}},
        {"mt03.notes",    "Show notes", 0, 1, 0,   Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"mt03.freq",     "Show freq", 0, 1, 1,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

namespace {
constexpr double kPi = 3.14159265358979323846, kLo = 20.0, kHi = 20000.0;
double mel(double f) { return 2595.0 * std::log10(1.0 + f / 700.0); }
double melInv(double m) { return 700.0 * (std::pow(10.0, m / 2595.0) - 1.0); }
double edge(int scale, double x) {   // x = 0 .. kBands: the lower edge of band x
    const double u = x / kBands;
    if (scale == Linear) return kLo + (kHi - kLo) * u;
    if (scale == Mel) return melInv(mel(kLo) + (mel(kHi) - mel(kLo)) * u);
    return kLo * std::pow(kHi / kLo, u);
}
}

double bandHz(int scale, int band) { return 0.5 * (edge(scale, band) + edge(scale, band + 1)); }
int bandOf(int scale, double hz) {
    if (hz <= kLo) return 0; if (hz >= kHi) return kBands - 1;
    double u;
    if (scale == Linear) u = (hz - kLo) / (kHi - kLo); else if (scale == Mel) u = (mel(hz) - mel(kLo)) / (mel(kHi) - mel(kLo)); else u = std::log(hz / kLo) / std::log(kHi / kLo);
    return std::clamp(static_cast<int>(u * kBands), 0, kBands - 1);
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    fft_.setup(kFft); work_.assign(kFft, {0.0, 0.0}); win_.resize(kFft);
    for (int i = 0; i < kFft; ++i) win_[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / kFft);
    ring_.assign(kFft, 0.0f); cols_.assign(static_cast<size_t>(kMaxColumns) * kBands, -300.0f);
    wpos_ = 0; filled_ = 0; since_ = 0; head_ = 0; count_ = 0; pvMix_ = 0.0; pvOn_ = false;
    maxCols_ = std::min(kMaxColumns, static_cast<int>(std::lround(target_[Scroll] * fs_ / kHop)));
    for (auto& c : hp_) for (auto& f : c) f.reset(); for (auto& c : lp_) for (auto& f : c) f.reset();
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Scroll && prepared_) { maxCols_ = std::min(kMaxColumns, static_cast<int>(std::lround(v * fs_ / kHop))); count_ = std::min(count_, maxCols_); }
}

void Processor::setPreview(bool on, double lo, double hi) {
    pvOn_ = on; pvLo_ = std::clamp(std::min(lo, hi), 20.0, 20000.0); pvHi_ = std::clamp(std::max(lo, hi), pvLo_ * 1.05, 0.45 * fs_);
    { const double fc = std::sqrt(pvLo_ * pvHi_), x1 = fc / pvLo_, x2 = fc / pvHi_, g = (x1 * x1 * x1 * x1 / (1.0 + x1 * x1 * x1 * x1)) / (1.0 + x2 * x2 * x2 * x2); pvGain_ = std::min(4.0, 1.0 / std::max(g, 1e-3)); }   // the cascade's loss at the centre is made up (the listening level)
    for (int c = 0; c < 2; ++c) for (int k = 0; k < 2; ++k) { hp_[static_cast<size_t>(c)][static_cast<size_t>(k)].setup(Svf::Mode::HighPass, pvLo_, fs_, 0.7071, 0.0); lp_[static_cast<size_t>(c)][static_cast<size_t>(k)].setup(Svf::Mode::LowPass, pvHi_, fs_, 0.7071, 0.0); }
}

void Processor::frame() {
    for (int i = 0; i < kFft; ++i) work_[static_cast<size_t>(i)] = {win_[static_cast<size_t>(i)] * ring_[static_cast<size_t>((wpos_ + i) % kFft)], 0.0};
    fft_.forward(work_);
    const int scale = static_cast<int>(target_[Scale] + 0.5);
    const double sc = 4.0 / kFft, floorDb = target_[Floor], binHz = fs_ / kFft;
    float* col = &cols_[static_cast<size_t>(head_) * kBands];
    for (int b = 0; b < kBands; ++b) {
        const double f0 = edge(scale, b), f1 = edge(scale, b + 1);
        int k0 = static_cast<int>(std::floor(f0 / binHz)), k1 = static_cast<int>(std::ceil(f1 / binHz));
        k0 = std::clamp(k0, 1, kFft / 2); k1 = std::clamp(std::max(k1, k0 + 1), k0 + 1, kFft / 2 + 1);
        double p = 0.0; for (int k = k0; k < k1; ++k) p = std::max(p, std::norm(work_[static_cast<size_t>(k)]) * sc * sc);   // the strongest bin in the band: a tone keeps its level
        col[b] = static_cast<float>(std::max(p > 1e-30 ? 10.0 * std::log10(p) : -300.0, floorDb));
    }
    head_ = (head_ + 1) % kMaxColumns; count_ = std::min(count_ + 1, maxCols_);
}

void Processor::column(int index, std::vector<float>& out) const {
    out.assign(kBands, -300.0f); if (index < 0 || index >= count_) return;
    const int pos = ((head_ - count_ + index) % kMaxColumns + kMaxColumns) % kMaxColumns;
    std::copy(&cols_[static_cast<size_t>(pos) * kBands], &cols_[static_cast<size_t>(pos) * kBands] + kBands, out.begin());
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const double step = 1.0 / (0.02 * fs_);
    for (int i = 0; i < n; ++i) {
        const double m = nch > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i];
        ring_[static_cast<size_t>(wpos_)] = static_cast<float>(m); wpos_ = (wpos_ + 1) % kFft; if (filled_ < kFft) ++filled_;
        if (++since_ >= kHop) { since_ = 0; if (filled_ >= kFft) frame(); }
        if (pvOn_ || pvMix_ > 0.0) {
            pvMix_ = pvOn_ ? std::min(1.0, pvMix_ + step) : std::max(0.0, pvMix_ - step);
            for (int c = 0; c < nch; ++c) {
                double y = ch[c][i];
                for (int k = 0; k < 2; ++k) y = lp_[static_cast<size_t>(c)][static_cast<size_t>(k)].process(hp_[static_cast<size_t>(c)][static_cast<size_t>(k)].process(y));
                y *= pvGain_;
                ch[c][i] = static_cast<float>(ch[c][i] * (1.0 - pvMix_) + y * pvMix_);
            }
        }
    }
}

}  // namespace sw::mt03
