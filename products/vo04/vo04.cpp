#include "vo04/vo04.hpp"
#include <algorithm>
#include <cmath>

namespace sw::vo04 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"vo04.voices",   "Voices",    1, 8, 2,   Curve::Step, 1, {1, 2, 4, 8}, "", {"1", "2", "4", "8"}},
            {"vo04.spread",   "Spread",    0, 10, 6,  Curve::Lin, 1, {}, ""},
            {"vo04.timing",   "Timing",    0, 10, 4,  Curve::Lin, 1, {}, ""},
            {"vo04.pitchvar", "Pitch var", 0, 10, 3,  Curve::Lin, 1, {}, ""},
            {"vo04.tone",     "Tone",      0, 100, 50, Curve::Lin, 1, {}, "%"},
            {"vo04.mix",      "Mix",       0, 100, 50, Curve::Lin, 1, {}, "%"},
        };
        v[Tone].minLabel = "Dark"; v[Tone].maxLabel = "Bright";
        return v;
    }();
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846, kMinMs = 0.5, kTimingMs = 3.0, kSlewMax = 0.005, kPitchCents = 0.8, kCentsToRatio = 0.000577622650466621;   // ln 2 / 1200
double frac(double x) { return x - std::floor(x); }
double wanderHz(int v, int k) { return 0.02 + 0.05 * frac((v + 1) * 0.6180339887 + 0.37 * k); }
double wanderPh(int v, int k) { return 2.0 * kPi * frac((v + 2) * 0.7548776662 + 0.21 * k); }
double pitchHz(int v, int k) { return 0.7 + 1.6 * frac((v + 1) * 0.6180339887 * (k + 1) + 0.3 * k); }
double pitchPh(int v, int k) { return 2.0 * kPi * frac((v + 3) * 0.5698402909 + 0.33 * k); }
}
double voicePan(int v, int n, double spread) { return n < 2 ? 0.5 : 0.5 + std::clamp(spread * 0.1, 0.0, 1.0) * (static_cast<double>(v) / (n - 1) - 0.5); }

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::setTone() {
    const double t = (target_[Tone] - 50.0) / 50.0;
    for (auto& c : ch_) { c.hi.setup(Svf::Mode::HighShelf, 3000.0, fs_, 0.7071, 8.0 * t); c.lo.setup(Svf::Mode::LowShelf, 400.0, fs_, 0.7071, -4.0 * t); }
    toneState_ = target_[Tone];
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    size_t sz = 16; while (sz < static_cast<size_t>(0.05 * fs_) + 64) sz <<= 1;
    mask_ = sz - 1;
    for (auto& b : buf_) b.assign(sz, 0.0f);
    for (auto& c : ch_) { c.hi.reset(); c.lo.reset(); }
    pos_ = 0; env_ = 0.0; t_ = 0.0; silent_ = 0;
    for (int v = 0; v < kMaxVoices; ++v) { base_[v] = kMinMs * 0.001 * fs_; lastDelay_[v] = base_[v]; }
    setTone();
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::snapToTargets() { if (prepared_) setTone(); }

double Processor::read(int c, double d) const {
    const auto& b = buf_[static_cast<size_t>(c)];
    const double rp = static_cast<double>(pos_) - d - 1.0;   // the newest sample sits at delay 1 after the write
    const double fl = std::floor(rp), f = rp - fl;
    const long i = static_cast<long>(fl);
    auto at = [&](long k) { return static_cast<double>(b[static_cast<size_t>(k) & mask_]); };
    const double y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const double c0 = y1, c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + c0;
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    if (target_[Tone] != toneState_) setTone();
    const int voices = std::clamp(static_cast<int>(target_[Voices] + 0.5), 1, kMaxVoices);
    const double minD = kMinMs * 0.001 * fs_, timingD = target_[Timing] * kTimingMs * 0.001 * fs_;
    // Pitch var: three sines per voice; slope rms = A x sqrt(mean over k of (2 pi f_k)^2 / 2) / fs per sample, set to 0.8 x Pitch var cents
    const double wantRatio = kPitchCents * target_[PitchVar] * kCentsToRatio;
    double gl[kMaxVoices], gr[kMaxVoices], amp[kMaxVoices], norm[2] = {0.0, 0.0};
    for (int v = 0; v < voices; ++v) {
        const double p = voicePan(v, voices, target_[Spread]); gl[v] = 1.0 - p; gr[v] = p; norm[0] += gl[v] * gl[v]; norm[1] += gr[v] * gr[v];
        double m = 0; for (int k = 0; k < 3; ++k) { const double w = 2.0 * kPi * pitchHz(v, k); m += 0.5 * w * w; }
        amp[v] = wantRatio * fs_ * std::sqrt(3.0) / std::sqrt(m);   // each of the 3 sines has amplitude amp/sqrt(3), so the slope rms is amp x sqrt(mean (w^2)/2) / sqrt(3)... (checked by the test: 0.8 x Pitch var cents)
    }
    const double nrm = 1.0 / std::sqrt(0.5 * (norm[0] + norm[1]));
    const double dt = 1.0 / fs_, rel = std::exp(-1.0 / (0.05 * fs_));
    const long silentLimit = static_cast<long>(0.1 * fs_);
    for (int i = 0; i < n; ++i) {
        double peak = 0.0;
        for (int c = 0; c < 2; ++c) { const double x = c < nch ? ch[c][i] : ch[0][i]; buf_[static_cast<size_t>(c)][pos_ & mask_] = static_cast<float>(x); peak = std::max(peak, std::abs(x)); }
        ++pos_;
        env_ = std::max(peak, env_ * rel);
        silent_ = env_ < 1e-3 ? silent_ + 1 : 0;
        const bool rest = silent_ >= silentLimit;
        t_ += dt;
        double out[2] = {0.0, 0.0};
        for (int v = 0; v < voices; ++v) {
            const double u = 0.575 + 0.425 * 0.5 * (std::sin(2.0 * kPi * wanderHz(v, 0) * t_ + wanderPh(v, 0)) + std::sin(2.0 * kPi * wanderHz(v, 1) * t_ + wanderPh(v, 1)));
            const double target = minD + timingD * u;
            if (rest) base_[v] = minD;   // nothing is sounding: the next phrase starts with the smallest delay
            else base_[v] += std::clamp(target - base_[v], -kSlewMax, kSlewMax);
            double e = 0.0;
            if (amp[v] > 0.0) for (int k = 0; k < 3; ++k) e += std::sin(2.0 * kPi * pitchHz(v, k) * t_ + pitchPh(v, k));
            const double d = std::max(base_[v] + e * amp[v] / std::sqrt(3.0), 2.0);
            lastDelay_[v] = d;
            out[0] += gl[v] * read(0, d); out[1] += gr[v] * read(1, d);
        }
        for (int c = 0; c < nch; ++c) {
            auto& cc = ch_[static_cast<size_t>(c)];
            double y = cc.lo.process(cc.hi.process(out[c] * nrm));
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::vo04
