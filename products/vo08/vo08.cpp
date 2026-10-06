#include "vo08/vo08.hpp"
#include <algorithm>
#include <cmath>

namespace sw::vo08 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"vo08.mode",        "Mode",        0, 2, 0,   Curve::Step, 1, {0, 1, 2}, "", {"Reduce", "Remove", "Mark only"}},
        {"vo08.reduction",   "Reduction",   -40, 0, -12, Curve::Lin, 1, {}, "dB"},
        {"vo08.sensitivity", "Sensitivity", 0, 2, 1,   Curve::Step, 1, {0, 1, 2}, "", {"Low", "Mid", "High"}},
        {"vo08.keep",        "Keep",        0, 2, 0,   Curve::Step, 1, {0, 1, 2}, "", {"Natural", "Less", "None"}},
        {"vo08.fade",        "Fade",        1, 50, 10, Curve::Log, 1, {}, "ms"},
    };
    return s;
}

namespace {
constexpr double kZcr[3] = {0.16, 0.12, 0.09}, kQuietDb[3] = {20.0, 15.0, 10.0}, kKeep[3] = {0.6, 1.0, 1.5};
constexpr double kPeakFall = 3.0, kVoiced = 0.6, kFloorDb = -80.0, kRemoveDb = 60.0, kMaxDb = 40.0, kBackMs = 10.0;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    hop_ = std::max(16, static_cast<int>(std::lround(0.004 * fs_))); win_ = 3 * hop_;
    dec_ = std::max(1, static_cast<int>(std::lround(fs_ / 12000.0)));
    lagLo_ = std::max(2, static_cast<int>(std::lround(fs_ / 1200.0))); lagHi_ = static_cast<int>(std::lround(fs_ / 80.0));
    for (auto& d : dly_) d.assign(static_cast<size_t>(kLatency), 0.0f);
    ring_.assign(static_cast<size_t>(win_), 0.0f); tmp_.assign(static_cast<size_t>(win_), 0.0); decd_.assign(static_cast<size_t>(win_ / dec_ + 1), 0.0);
    rpos_ = 0; since_ = 0; dpos_ = 0; run_ = 0; count_ = 0; inBreath_ = false; peakDb_ = -200.0; gainDb_ = 0.0;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

double Processor::depthDb() const {
    const int mode = static_cast<int>(target_[Mode] + 0.5);
    if (mode == MarkOnly) return 0.0;
    if (mode == Remove) return kRemoveDb;
    return std::min(kMaxDb, -target_[Reduction] * kKeep[std::clamp(static_cast<int>(target_[Keep] + 0.5), 0, 2)]);
}

void Processor::frame() {
    const int sens = std::clamp(static_cast<int>(target_[Sensitivity] + 0.5), 0, 2);
    double e = 0.0, prev = 0.0; int zc = 0;
    for (int i = 0; i < win_; ++i) { const double x = ring_[static_cast<size_t>((rpos_ + i) % win_)]; tmp_[static_cast<size_t>(i)] = x; e += x * x; if (i > 0 && ((x >= 0.0) != (prev >= 0.0))) ++zc; prev = x; }
    const double ms = e / win_, db = ms > 1e-16 ? 10.0 * std::log10(ms) : -160.0, zcr = static_cast<double>(zc) / (win_ - 1);
    peakDb_ = std::max(db, peakDb_ - kPeakFall * hop_ / fs_);
    bool breath = db > kFloorDb && db < peakDb_ - kQuietDb[sens] && zcr > kZcr[sens];
    if (breath) {   // not voiced: the normalised autocorrelation of the decimated frame has no peak
        const int nd = win_ / dec_;
        for (int k = 0; k < nd; ++k) { double a = 0.0; for (int j = 0; j < dec_; ++j) a += tmp_[static_cast<size_t>(k * dec_ + j)]; decd_[static_cast<size_t>(k)] = a / dec_; }
        double e0 = 0.0; for (int k = 0; k < nd; ++k) e0 += decd_[static_cast<size_t>(k)] * decd_[static_cast<size_t>(k)];
        double best = 0.0;
        const int lo = std::max(2, lagLo_ / dec_), hi = std::min(lagHi_ / dec_, nd - 16);
        for (int l = lo; l <= hi; ++l) {
            double r = 0.0, e1 = 0.0; for (int k = 0; k + l < nd; ++k) { r += decd_[static_cast<size_t>(k)] * decd_[static_cast<size_t>(k + l)]; e1 += decd_[static_cast<size_t>(k + l)] * decd_[static_cast<size_t>(k + l)]; }
            if (e0 > 1e-20 && e1 > 1e-20) best = std::max(best, r / std::sqrt(e0 * e1));
        }
        if (best >= kVoiced) breath = false;
    }
    if (breath) { if (++run_ >= 2 && !inBreath_) { inBreath_ = true; ++count_; } }
    else { run_ = 0; inBreath_ = false; }
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const double depth = depthDb();
    const double fadeMs = target_[Fade];
    const double down = std::max(depth, 1.0) / (fadeMs * 0.001 * fs_), up = std::max(depth, 1.0) / (std::min(fadeMs, kBackMs) * 0.001 * fs_);
    for (int i = 0; i < n; ++i) {
        double m = 0.0;
        for (int c = 0; c < nch; ++c) m += ch[c][i];
        ring_[static_cast<size_t>(rpos_)] = static_cast<float>(m / std::max(1, nch)); rpos_ = (rpos_ + 1) % win_;
        if (++since_ >= hop_) { since_ = 0; frame(); }
        const double want = (inBreath_ && depth > 0.0) ? -depth : 0.0;
        if (want < gainDb_) gainDb_ = std::max(want, gainDb_ - down); else gainDb_ = std::min(want, gainDb_ + up);
        const double g = gainDb_ == 0.0 ? 1.0 : std::pow(10.0, gainDb_ / 20.0);
        for (int c = 0; c < nch; ++c) {
            auto& d = dly_[static_cast<size_t>(c)];
            const double y = d[static_cast<size_t>(dpos_)] * g;
            d[static_cast<size_t>(dpos_)] = ch[c][i];
            ch[c][i] = std::abs(y) < 1e-30 || !std::isfinite(y) ? 0.0f : static_cast<float>(y);
        }
        dpos_ = (dpos_ + 1) % kLatency;
    }
}

}  // namespace sw::vo08
