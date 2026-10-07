#include "lv20/lv20.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv20 {
namespace { constexpr int kN = 16384, kHop = 4096; }

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv20.res",    "Resolution", 0, 2, 0,   Curve::Step, 1, {0, 1, 2}, "", {"1/3 oct", "1/6 oct", "1/12 oct"}},
        {"lv20.speed",  "Speed",      0, 2, 1,   Curve::Step, 1, {0, 1, 2}, "", {"Slow", "Medium", "Fast"}},
        {"lv20.hold",   "Peak hold",  0, 10, 2,  Curve::Lin, 1, {}, "s"},
        {"lv20.weight", "Weight",     0, 2, 0,   Curve::Step, 1, {0, 1, 2}, "", {"Z", "A", "C"}},
        {"lv20.pink",   "Pink ref",   0, 1, 1,   Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"lv20.freeze", "Freeze",     0, 1, 0,   Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

double weightingDb(int type, double f) {
    if (type == 0) return 0.0;
    const double f2 = f * f, c1 = 20.6 * 20.6, c4 = 12194.0 * 12194.0;
    if (type == 2) { const double r = (c4 * f2) / ((f2 + c1) * (f2 + c4)); return 20.0 * std::log10(r) + 0.06; }
    const double r = (c4 * f2 * f2) / ((f2 + c1) * std::sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) * (f2 + c4));
    return 20.0 * std::log10(r) + 2.0;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int maxBlock) {
    fs_ = sampleRate; fft_.setup(kN); win_.resize(kN); for (int i = 0; i < kN; ++i) win_[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / kN);
    ring_.assign(kN, 0.0); buf_.assign(kN, {}); pow_.assign(kN / 2 + 1, 0.0); pos_ = 0; since_ = 0; frames_ = 0; layoutRes_ = -1;
    smooth_.fill(0.0); shown_.fill(-200.0); peak_.fill(-200.0); hold_.fill(0.0);
    mono_.assign(static_cast<size_t>(std::max(1, maxBlock) + 8), 0.0f); prepared_ = true; layout();
}
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); if (prepared_ && (id == Resolution || id == Weight)) layout(); }

void Processor::layout() {
    const int res = static_cast<int>(target_[Resolution] + 0.5), N = res == 0 ? 3 : res == 1 ? 6 : 12;
    nb_ = 0;
    for (int k = -6 * N; ; ++k) {   // 1000 Hz x 2^(k/N); the first centre at or above 19.5 Hz
        const double fc = 1000.0 * std::pow(2.0, static_cast<double>(k) / N);
        if (fc < 19.5) continue;
        if (fc > 20500.0 || nb_ >= kMaxBands) break;
        if (fc >= fs_ * 0.48) break;
        const size_t b = static_cast<size_t>(nb_++); centre_[b] = fc; lo_[b] = fc * std::pow(2.0, -0.5 / N); hi_[b] = fc * std::pow(2.0, 0.5 / N);
    }
    for (int b = 0; b < nb_; ++b) wgt_[static_cast<size_t>(b)] = std::pow(10.0, weightingDb(static_cast<int>(target_[Weight] + 0.5), centre_[static_cast<size_t>(b)]) / 10.0);
    if (layoutRes_ != res || layoutRes_ < 0) { smooth_.fill(0.0); shown_.fill(-200.0); peak_.fill(-200.0); hold_.fill(0.0); }
    layoutRes_ = res;
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    for (int i = 0; i < n; ++i) {
        ring_[static_cast<size_t>(pos_)] = numCh > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i]; pos_ = (pos_ + 1) % kN;
        if (++since_ >= kHop) { since_ = 0; if (target_[Freeze] < 0.5) analyse(); }
    }
}

void Processor::analyse() {
    for (int i = 0; i < kN; ++i) buf_[static_cast<size_t>(i)] = ring_[static_cast<size_t>((pos_ + i) % kN)] * win_[static_cast<size_t>(i)];
    fft_.forward(buf_);
    double sw2 = 0; for (double w : win_) sw2 += w * w;
    const double scale = 2.0 / (static_cast<double>(kN) * sw2), binHz = fs_ / kN;   // a full-scale sine: -3.01 dB in the band that holds it
    for (int k = 0; k <= kN / 2; ++k) pow_[static_cast<size_t>(k)] = std::norm(buf_[static_cast<size_t>(k)]) * scale;
    static constexpr double kTau[3] = {1.0, 0.3, 0.1};
    const double tau = kTau[std::clamp(static_cast<int>(target_[Speed] + 0.5), 0, 2)], a = 1.0 - std::exp(-(static_cast<double>(kHop) / fs_) / tau), dt = static_cast<double>(kHop) / fs_;
    const double holdS = target_[PeakHold];
    for (int b = 0; b < nb_; ++b) {
        const size_t i = static_cast<size_t>(b); double p = 0;
        const int k0 = static_cast<int>(std::ceil(lo_[i] / binHz)), k1 = static_cast<int>(std::ceil(hi_[i] / binHz)) - 1;
        if (k1 - k0 + 1 >= 2) { for (int k = std::max(1, k0); k <= std::min(kN / 2, k1); ++k) p += pow_[static_cast<size_t>(k)]; }
        else { const double x = centre_[i] / binHz; const int k = static_cast<int>(std::floor(x)); const double fr = x - k; p = ((1 - fr) * pow_[static_cast<size_t>(std::clamp(k, 1, kN / 2))] + fr * pow_[static_cast<size_t>(std::clamp(k + 1, 1, kN / 2))]); }
        p *= wgt_[i];
        smooth_[i] = frames_ == 0 ? p : smooth_[i] + a * (p - smooth_[i]);
        const double db = smooth_[i] > 1e-20 ? 10.0 * std::log10(smooth_[i]) : -200.0; shown_[i] = db;
        if (holdS <= 0.0) peak_[i] = db;
        else { if (db >= peak_[i]) { peak_[i] = db; hold_[i] = holdS; } else if (hold_[i] > 0.0) hold_[i] -= dt; else peak_[i] = std::max(db, peak_[i] - 20.0 * dt); }
    }
    ++frames_;
}

double Processor::pinkRefDb() const {
    double s = 0; int c = 0;
    for (int b = 0; b < nb_; ++b) if (centre_[static_cast<size_t>(b)] >= 100.0 && centre_[static_cast<size_t>(b)] <= 10000.0 && shown_[static_cast<size_t>(b)] > -150.0) { s += shown_[static_cast<size_t>(b)]; ++c; }
    return c ? s / c : -200.0;
}

}  // namespace sw::lv20
