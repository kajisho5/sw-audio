#include "rs03/rs03.hpp"
#include <algorithm>
#include <cmath>

namespace sw::rs03 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"rs03.base",      "Base Hz",   0, 2, 0,    Curve::Step, 1, {0, 1, 2}, "", {"50", "60", "Auto"}},
            {"rs03.harmonics", "Harmonics", 2, 16, 8,   Curve::Step, 1, {2, 4, 8, 16}, "", {"2", "4", "8", "16"}},
            {"rs03.depth",     "Depth",     0, 10, 5,   Curve::Lin, 1, {}, ""},
            {"rs03.width",     "Width",     0, 100, 0,  Curve::Lin, 1, {}, "%"},
            {"rs03.buzz",      "Buzz",      0, 10, 0,   Curve::Lin, 1, {}, ""},
            {"rs03.evo.on",    "Track",     0, 1, 1,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        };
        v[Width].minLabel = "Narrow"; v[Width].maxLabel = "Wide";
        return v;
    }();
    return s;
}

namespace {
constexpr double kPi = 3.14159265358979323846, kSpan = 2.0, kStep = 0.25;
double candHz(int group, int j) { return (group == 0 ? 50.0 : 60.0) - kSpan + kStep * j; }
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

double Processor::effectiveBase() const {
    const int b = static_cast<int>(target_[Base] + 0.5);
    return b == Hz60 ? 60.0 : (b == Auto ? (autoGroup_ == 1 ? 60.0 : 50.0) : 50.0);
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    dec_ = std::max(1, static_cast<int>(std::lround(fs_ / 1500.0))); fd_ = fs_ / dec_; winLen_ = static_cast<int>(std::lround(fd_));
    lpC_ = 1.0 - std::exp(-2.0 * kPi * 300.0 / fs_);
    acc_.assign(static_cast<size_t>(2 * kCand * kHarmTrack), {0.0, 0.0}); power_.assign(static_cast<size_t>(kCand), 0.0);
    lp1_ = lp2_ = 0.0; decCount_ = 0; winPos_ = 0; autoGroup_ = 0; votePick_ = -1; votes_ = 0;
    for (auto& c : ch_) for (auto& f : c.notch) f.reset();
    f0_ = effectiveBase(); lastF0_ = -1.0; lastKey_ = -1.0;
    prepared_ = true;
    setFilters(0);
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Base && prepared_) { if (target_[Base] < 1.5) { autoGroup_ = static_cast<int>(target_[Base] + 0.5); } f0_ = effectiveBase(); }
}

void Processor::setFilters(int ramp) {
    const int H = std::clamp(static_cast<int>(target_[Harmonics] + 0.5), 1, 16);
    const double q = 60.0 * std::pow(8.0 / 60.0, target_[Width] * 0.01), buzz = target_[Buzz];
    int used = 0;
    for (int h = 1; h <= kMaxNotches; ++h) {
        const size_t i = static_cast<size_t>(h - 1);
        double depth = 0.0;
        if (h <= H) depth = 4.0 * target_[Depth] + ((h & 1) ? buzz : 0.0);
        else if (h <= 2 * H && buzz > 0.0) depth = 4.0 * buzz;
        depth = std::min(depth, 40.0);
        const double f = h * f0_;
        active_[i] = depth > 0.0 && f < 0.45 * fs_;
        if (active_[i]) { used = h; for (auto& c : ch_) c.notch[i].setupRamp(Svf::Mode::Bell, f, fs_, q, -depth, ramp); }
    }
    (void)used;
    lastF0_ = f0_; lastKey_ = target_[Depth] * 1e4 + target_[Width] * 10 + target_[Buzz] * 1e-2 + target_[Harmonics] * 1e6;
}

void Processor::trackerPush(double x) {
    lp1_ += lpC_ * (x - lp1_); lp2_ += lpC_ * (lp1_ - lp2_);
    if (++decCount_ < dec_) return;
    decCount_ = 0;
    const double d = lp2_;
    const bool autoMode = target_[Base] > 1.5;
    const int groups = autoMode ? 2 : 1, g0 = autoMode ? 0 : (target_[Base] > 0.5 ? 1 : 0);
    for (int gi = 0; gi < groups; ++gi) {
        const int g = autoMode ? gi : g0;
        for (int j = 0; j < kCand; ++j) for (int h = 1; h <= kHarmTrack; ++h) {
            const double w = -2.0 * kPi * h * candHz(g, j) * winPos_ / fd_;
            acc_[static_cast<size_t>((g * kCand + j) * kHarmTrack + (h - 1))] += d * std::complex<double>(std::cos(w), std::sin(w));
        }
    }
    if (++winPos_ >= winLen_) { trackerEvaluate(); winPos_ = 0; std::fill(acc_.begin(), acc_.end(), std::complex<double>(0.0, 0.0)); }
}

void Processor::trackerEvaluate() {
    const bool autoMode = target_[Base] > 1.5, track = target_[Track] > 0.5;
    const int groups = autoMode ? 2 : 1, g0 = autoMode ? 0 : (target_[Base] > 0.5 ? 1 : 0);
    double bestRatio = 0.0, bestEst = 0.0; int bestGroup = g0; bool any = false;
    for (int gi = 0; gi < groups; ++gi) {
        const int g = autoMode ? gi : g0;
        for (int j = 0; j < kCand; ++j) { double p = 0.0; for (int h = 1; h <= kHarmTrack; ++h) p += std::norm(acc_[static_cast<size_t>((g * kCand + j) * kHarmTrack + (h - 1))]) / (h * h > 0 ? 1.0 : 1.0); power_[static_cast<size_t>(j)] = p; }
        int pk = 0; for (int j = 1; j < kCand; ++j) if (power_[static_cast<size_t>(j)] > power_[static_cast<size_t>(pk)]) pk = j;
        std::vector<double>& pw = power_;
        double sorted[kCand]; std::copy(pw.begin(), pw.begin() + kCand, sorted); std::sort(sorted, sorted + kCand);
        const double med = sorted[kCand / 2] + 1e-30, ratio = pw[static_cast<size_t>(pk)] / med;
        double est = candHz(g, pk);
        if (pk > 0 && pk < kCand - 1) {
            const double a = std::log(pw[static_cast<size_t>(pk - 1)] + 1e-30), b = std::log(pw[static_cast<size_t>(pk)] + 1e-30), c = std::log(pw[static_cast<size_t>(pk + 1)] + 1e-30), den = a - 2.0 * b + c;
            if (den < -1e-9) est += kStep * 0.5 * (a - c) / den;
        }
        if (ratio > bestRatio) { bestRatio = ratio; bestEst = est; bestGroup = g; any = true; }
    }
    if (!any || bestRatio < 2.0) return;
    if (autoMode) {
        if (bestGroup == votePick_) ++votes_; else { votePick_ = bestGroup; votes_ = 1; }
        if (votes_ >= 2 && bestGroup != autoGroup_) { autoGroup_ = bestGroup; f0_ = bestGroup == 1 ? 60.0 : 50.0; }
    }
    if (track && (!autoMode || bestGroup == autoGroup_)) f0_ += 0.5 * (std::clamp(bestEst, effectiveBase() - kSpan, effectiveBase() + kSpan) - f0_);
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const double key = target_[Depth] * 1e4 + target_[Width] * 10 + target_[Buzz] * 1e-2 + target_[Harmonics] * 1e6;
    const bool follow = target_[Track] > 0.5 || target_[Base] > 1.5;
    for (int off = 0; off < n; off += kBlock) {
        const int len = std::min(kBlock, n - off);
        if (follow) for (int i = 0; i < len; ++i) { double m = 0.0; for (int c = 0; c < nch; ++c) m += ch[c][off + i]; trackerPush(m / std::max(1, nch)); }
        if (std::abs(f0_ - lastF0_) > 0.005 || key != lastKey_) setFilters(kBlock);
        for (int c = 0; c < nch; ++c) {
            auto& nt = ch_[static_cast<size_t>(c)].notch;
            for (int i = 0; i < len; ++i) {
                double y = ch[c][off + i];
                for (int h = 0; h < kMaxNotches; ++h) if (active_[static_cast<size_t>(h)]) y = nt[static_cast<size_t>(h)].process(y);
                if (!std::isfinite(y) || std::abs(y) < 1e-30) y = 0.0;
                ch[c][off + i] = static_cast<float>(y);
            }
        }
    }
}

}  // namespace sw::rs03
