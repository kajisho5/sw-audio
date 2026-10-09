#include "dy07/dy07.hpp"
#include <algorithm>
#include <cmath>

namespace sw::dy07 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"dy07.thresh", "Threshold", -40, 20, 20, Curve::Lin,  1, {}, "dB"},
            {"dy07.ratio"   ,  "Compress",  1, 20, 4,    Curve::Log,  1, {}, ":1"},
            {"dy07.out",       "Output",    -20, 20, 0,  Curve::Lin,  1, {}, "dB"},
            {"dy07.snap"    ,  "Snap",      -6, 6, 0,    Curve::Lin,  1, {}, "dB"},
            {"dy07.mix",       "Mix",       0, 100, 100, Curve::Lin,  1, {}, "%"},
            {"dy07.knee",      "Knee",      0, 1, 0,     Curve::Step, 1, {0, 1}, "", {"Soft knee", "Hard knee"}},
            unitSpec("dy07.unit"),
        };
        v[Compress].maxLabel = "inf";
        v[Compress].maxLabelNorm = 0.95;  // spec: rightmost 5 % is infinity
        return v;
    }();
    return s;
}

namespace { constexpr double kRefDbfs = -18.0, kReleaseDbPerS = 120.0; }

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& d : det_) d.set(fs_, LevelDetector::Mode::Rms);
    fastRel_ = Ballistics::coef(fs_, 5.0);
    slowCoef_ = Ballistics::coef(fs_, 30.0);
    snapCoef_ = Ballistics::coef(fs_, 1.0);
    releaseStep_ = kReleaseDbPerS / fs_;
    gr_ = snapGainDb_ = fast_ = slow_ = 0; releasing_ = false; releaseCount_ = 0;
    updateCurve();
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v));
    if (id == Threshold || id == Compress || id == Knee) updateCurve();
}

void Processor::updateCurve() {
    const double ratio = isInfiniteRatio(specs()[Compress], target_[Compress]) ? GainComputer::kInfinity : target_[Compress];
    comp_.set(target_[Threshold] + kRefDbfs, ratio, target_[Knee] < 0.5 ? 10.0 : 0.0);
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const double snap = target_[Snap];
    for (int i = 0; i < n; ++i) {
        double level = 0;
        for (int c = 0; c < nch; ++c) level = std::max(level, det_[static_cast<size_t>(c)].process(ch[c][i]));
        const double lv = 20.0 * std::log10(std::max(level, 1e-9));
        const double target = comp_.gainDb(lv);
        if (target < gr_) {  // attack: faster the further over the threshold
            const double over = std::max(0.0, lv - comp_.t_);
            const double c = Ballistics::coef(fs_, 15.0 / (1.0 + over / 10.0));
            gr_ = target + c * (gr_ - target);
            if (target < gr_ - 0.01) releasing_ = false;
        } else {             // release: constant slope
            if (!releasing_ && gr_ < -1.0 && target > gr_ + 0.01) { releasing_ = true; releaseCount_ = 0; }
            gr_ = std::min(target, gr_ + releaseStep_);
        }
        if (releasing_) {
            ++releaseCount_;
            if (gr_ >= -1.0) { recoveryMs_ = 1000.0 * releaseCount_ / fs_; releasing_ = false; }
        }
        const double g = std::pow(10.0, gr_ / 20.0);
        double peak = 0;
        for (int c = 0; c < nch; ++c) peak = std::max(peak, std::abs(ch[c][i] * g));
        double sg = 1.0;
        if (snap != 0.0) {  // transient heads: fast envelope above the slow one
            fast_ = std::max(peak, fastRel_ * fast_);
            slow_ = peak + slowCoef_ * (slow_ - peak);
            const double amount = slow_ > 1e-9 ? std::clamp(20.0 * std::log10(std::max(fast_, 1e-12) / slow_), 0.0, 6.0) : 0.0;
            const double want = snap * amount / 6.0;
            snapGainDb_ = want + snapCoef_ * (snapGainDb_ - want);
            sg = std::pow(10.0, snapGainDb_ / 20.0);
        }
        for (int c = 0; c < nch; ++c) {
            double y = ch[c][i] * g * sg;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::dy07
