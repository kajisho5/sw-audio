#include "dy06/dy06.hpp"
#include <algorithm>
#include <cmath>

namespace sw::dy06 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"dy06.input",  "Input",     0, 10, 3.3,  Curve::Lin,  1, {}, ""},
            {"dy06.thresh", "Threshold", 0, 10, 0,    Curve::Lin,  1, {}, ""},
            {"dy06.time",   "Time",      1, 6, 3,     Curve::Step, 1, {1, 2, 3, 4, 5, 6}, ""},
            {"dy06.mu",     "Mu",        0, 1, 0.5,   Curve::Lin,  1, {}, ""},
            {"dy06.mix",    "Mix",       0, 100, 100, Curve::Lin,  1, {}, "%"},
            {"dy06.stereo", "Stereo",    0, 1, 0,     Curve::Step, 1, {0, 1}, "", {"Link", "Dual"}},
            {"dy06.evo.on", "Density adapt", 0, 1, 1, Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        };
        v[Mu].minLabel = "Soft"; v[Mu].maxLabel = "Hard";
        return v;
    }();
    return s;
}

double inputDb(double knob) { return -10.0 + 3.0 * knob; }
double attackMs(int time) { static const double a[] = {2, 2, 4, 8, 4, 2}; return a[std::clamp(time, 1, 6) - 1]; }

namespace {
constexpr double kKneeDb = 6.0, kMaxOver = 60.0, kStep = 0.1;   // soft start 6 dB below the threshold
constexpr double kRatioLow = 1.5, kRatioHigh = 6.0;
constexpr double kReleaseS[4] = {0.3, 0.8, 1.5, 3.0};
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

// slope of the curve = 1 - 1/ratio(over); ratio(over) = 1.5 + 4.5 (1 - exp(-over / W)), W = 30 * (4/30)^Mu (design); a 12 dB soft start
void Processor::buildCurve() {
    const double w = 30.0 * std::pow(4.0 / 30.0, target_[Mu]);
    const int n = static_cast<int>((kMaxOver + kKneeDb) / kStep) + 1;
    curve_.assign(static_cast<size_t>(n), 0.0);
    double gr = 0;
    for (int k = 1; k < n; ++k) {
        const double o = -kKneeDb + k * kStep;
        const double r = kRatioLow + (kRatioHigh - kRatioLow) * (1.0 - std::exp(-std::max(o, 0.0) / w));
        const double t = std::clamp((o + kKneeDb) / (2.0 * kKneeDb), 0.0, 1.0);
        const double slope = (1.0 - 1.0 / r) * t * t * (3.0 - 2.0 * t);
        gr -= slope * kStep;
        curve_[static_cast<size_t>(k)] = gr;
    }
}
double Processor::staticGr(double over) const {
    if (over <= -kKneeDb) return 0.0;
    const double x = (over + kKneeDb) / kStep;
    const size_t i = static_cast<size_t>(x);
    if (i + 1 >= curve_.size()) return curve_.back() - (over - kMaxOver) * (1.0 - 1.0 / kRatioHigh);
    return curve_[i] + (curve_[i + 1] - curve_[i]) * (x - static_cast<double>(i));
}

void Processor::updateTiming() {
    const int t = static_cast<int>(target_[Time] + 0.5);
    attackC_ = Ballistics::coef(fs_, attackMs(t));
    autoRel_ = t >= 5;
    if (autoRel_) { fastRelC_ = Ballistics::coef(fs_, 500.0); slowRelC_ = Ballistics::coef(fs_, t == 5 ? 5000.0 : 10000.0); if (t == 6) fastRelC_ = Ballistics::coef(fs_, 300.0); slowAttC_ = Ballistics::coef(fs_, 1000.0); }
    else fixedRelS_ = kReleaseS[t - 1];
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& c : ch_) { c.det.set(fs_, LevelDetector::Mode::Program); c.reset(); }
    for (auto& d : tubeCh_) d.prepare(fs_, 3.0);
    in_.reset(fs_, 20.0, inputDb(target_[Input]));
    inGain_ = std::pow(10.0, in_.current() / 20.0);
    pkC_ = Ballistics::coef(fs_, 2000.0); msC_ = Ballistics::coef(fs_, 2000.0);
    envFastC_ = Ballistics::coef(fs_, 5.0); envSlowC_ = Ballistics::coef(fs_, 30.0); onsetC_ = Ballistics::coef(fs_, 2000.0);
    scaleC_ = Ballistics::coef(fs_, 1000.0);
    pk_ = ms_ = envFast_ = envSlow_ = onsetRate_ = 0; onset_ = false; relScale_ = 1.0; gr_ = 0;
    buildCurve();
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Input) in_.setTarget(inputDb(v));
    else if (id == Mu) buildCurve();
    else if (id == Time) updateTiming();
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const bool link = target_[Stereo] < 0.5, adapt = target_[Density] > 0.5;
    const double thr = -3.0 * target_[Threshold];
    for (int i = 0; i < n; ++i) {
        if (in_.isSmoothing()) inGain_ = std::pow(10.0, in_.next() / 20.0);
        double x[2] = {0, 0}, lv[2] = {-200, -200}, peak = 0;
        for (int c = 0; c < nch; ++c) {
            tubeCh_[static_cast<size_t>(c)].tick();
            x[c] = tubeCh_[static_cast<size_t>(c)].process(0, ch[c][i] * inGain_);
            lv[c] = 20.0 * std::log10(std::max(ch_[static_cast<size_t>(c)].det.process(x[c]), 1e-9));
            peak = std::max(peak, std::abs(x[c]));
        }
        if (nch > 1 && link) lv[0] = lv[1] = std::max(lv[0], lv[1]);
        // density: crest factor over 2 s and the onset rate -> release scale 0.5 .. 2
        pk_ = std::max(peak, pkC_ * pk_);
        ms_ = peak * peak + msC_ * (ms_ - peak * peak);
        envFast_ = std::max(peak, envFastC_ * envFast_);
        envSlow_ = peak + envSlowC_ * (envSlow_ - peak);
        const bool on = envFast_ > 2.0 * envSlow_ && envFast_ > 1e-3;
        onsetRate_ *= onsetC_;   // events per second, 2 s average
        if (on && !onset_) onsetRate_ += (1.0 - onsetC_) * fs_;
        onset_ = on;
        double want = 1.0;
        if (adapt) {
            const double crest = ms_ > 1e-12 ? 20.0 * std::log10(pk_ / std::sqrt(ms_)) : 20.0;
            const double d = 0.85 * std::clamp((16.0 - crest) / 8.0, 0.0, 1.0) + 0.15 * std::clamp(onsetRate_ / 6.0, 0.0, 1.0);
            want = 0.5 * std::pow(4.0, d);
        }
        relScale_ = want + scaleC_ * (relScale_ - want);
        double g[2] = {1, 1};
        for (int c = 0; c < nch; ++c) {
            Chan& s = ch_[static_cast<size_t>(c)];
            const double target = staticGr(lv[c] - thr);
            double gr;
            if (autoRel_) {
                s.fast = target < s.fast ? target + attackC_ * (s.fast - target) : target + fastRelC_ * (s.fast - target);
                s.slow = target < s.slow ? target + slowAttC_ * (s.slow - target) : target + slowRelC_ * (s.slow - target);
                gr = 0.5 * (s.fast + s.slow);
            } else {
                const double rc = std::exp(-1.0 / (fixedRelS_ * relScale_ * fs_));
                s.fast = target < s.fast ? target + attackC_ * (s.fast - target) : target + rc * (s.fast - target);
                gr = s.fast;
            }
            if (c == 0) gr_ = gr;
            g[c] = std::pow(10.0, gr / 20.0);
        }
        for (int c = 0; c < nch; ++c) {
            double y = x[c] * g[c];
            if (!std::isfinite(y)) y = 0.0;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::dy06
