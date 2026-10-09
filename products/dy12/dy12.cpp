#include "dy12/dy12.hpp"
#include <algorithm>
#include <cmath>

namespace sw::dy12 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"dy12.squash",  "Squash",  0, 10, 5,     Curve::Lin,  1, {}, ""},
        {"dy12.blend",   "Blend",   0, 100, 30,   Curve::Lin,  1, {}, "%"},
        {"dy12.upward",  "Upward",  0, 10, 0,     Curve::Lin,  1, {}, ""},
        {"dy12.tone",    "Tone",    -6, 6, 0,     Curve::Lin,  1, {}, "dB"},
        {"dy12.speed",   "Speed",   0, 3, 3,      Curve::Step, 1, {0, 1, 2, 3}, "", {"Fast", "Med", "Slow", "Auto"}},
        {"dy12.out",     "Output",  -10, 10, 0,   Curve::Lin,  1, {}, "dB"},
            unitSpec("dy12.unit"),
    };
    return s;
}

namespace {
constexpr double kRatio = 10.0, kKneeDb = 6.0, kRefDb = -12.0, kMaxLiftDb = 12.0;
constexpr double kToneHz = 1000.0;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

// 0 below -60 dBFS (no noise lift), full from -50 to -40, back to 0 at -20 (smooth in between); design shape of the spec's
// "lift what is below about -40 dBFS, up to +12 dB, nothing below -60 dBFS"
double Processor::upwardWeight(double l) {
    auto smooth = [](double x) { x = std::clamp(x, 0.0, 1.0); return x * x * (3.0 - 2.0 * x); };
    if (l <= -45.0) return smooth((l + 60.0) / 10.0);   // -60 -> -50: 0 -> 1
    return smooth((-20.0 - l) / 20.0 * 1.0 + 0.0);       // -40 -> -20: 1 -> 0
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& d : sq_) { d.set(fs_, LevelDetector::Mode::Program); d.reset(); }
    for (auto& d : up_) { d.set(fs_, LevelDetector::Mode::Rms); d.reset(); }
    liftAtt_ = Ballistics::coef(fs_, 20.0); liftRel_ = Ballistics::coef(fs_, 100.0);
    toneC_ = std::exp(-2.0 * 3.14159265358979323846 * kToneHz / fs_);
    lp_ = {0, 0};
    gr_ = lift_ = 0;
    fast_.reset(0.0); slow_.reset(0.0);
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Squash) {
        const double thr = -4.0 * v;
        comp_.set(thr, kRatio, kKneeDb);
        makeupDb_ = -comp_.gainDb(kRefDb);   // the loss at the -12 dBFS reference level
    } else if (id == Speed) updateTiming();
}

void Processor::updateTiming() {
    static const double a[3] = {1.0, 5.0, 20.0}, r[3] = {50.0, 150.0, 400.0};
    const int s = static_cast<int>(target_[Speed] + 0.5);
    autoSpeed_ = s == 3;
    if (autoSpeed_) { fast_.set(fs_, 3.0, 80.0); slow_.set(fs_, 100.0, 800.0); }   // two stages: short peaks recover fast, sustained pressure slowly
    else fast_.set(fs_, a[s], r[s]);
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const double blend = target_[Blend] / 100.0, tone = target_[Tone], upAmt = target_[Upward] / 10.0 * kMaxLiftDb;
    const double gLo = std::pow(10.0, -tone / 20.0), gHi = std::pow(10.0, tone / 20.0);
    for (int i = 0; i < n; ++i) {
        double x[2] = {0, 0}, sqLv = 0, upLv = 0;
        for (int c = 0; c < nch; ++c) {
            x[c] = ch[c][i];
            sqLv = std::max(sqLv, sq_[static_cast<size_t>(c)].process(x[c]));
            upLv = std::max(upLv, up_[static_cast<size_t>(c)].process(x[c]));
        }
        // squashed side
        const double target = comp_.gainDb(20.0 * std::log10(std::max(sqLv, 1e-9)));
        gr_ = fast_.process(target);
        if (autoSpeed_) gr_ = std::min(gr_, slow_.process(target));
        const double gSq = std::pow(10.0, (gr_ + makeupDb_) / 20.0);
        // dry side: upward
        double liftWant = upAmt > 0.0 ? upAmt * upwardWeight(20.0 * std::log10(std::max(upLv, 1e-9))) : 0.0;
        lift_ = liftWant + (liftWant > lift_ ? liftAtt_ : liftRel_) * (lift_ - liftWant);
        const double gUp = std::abs(lift_) < 1e-6 ? 1.0 : std::pow(10.0, lift_ / 20.0);
        for (int c = 0; c < nch; ++c) {
            double wet = x[c] * gSq;
            if (tone != 0.0) { lp_[static_cast<size_t>(c)] = wet + toneC_ * (lp_[static_cast<size_t>(c)] - wet); wet = lp_[static_cast<size_t>(c)] * gLo + (wet - lp_[static_cast<size_t>(c)]) * gHi; }
            double y = (1.0 - blend) * x[c] * gUp + blend * wet;
            if (!std::isfinite(y)) y = 0.0;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::dy12
