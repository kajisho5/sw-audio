#include "ms06/ms06.hpp"
#include <algorithm>
#include <cmath>

namespace sw::ms06 {

namespace {
const char* kNames[kStages] = {"EQ", "Comp", "Saturate", "Width", "Limit"};
int fact(int n) { int f = 1; for (int i = 2; i <= n; ++i) f *= i; return f; }
constexpr double kLookaheadMs = 1.5;
}

std::array<int, kStages> orderFromIndex(int index) {
    std::vector<int> left = {0, 1, 2, 3, 4};
    std::array<int, kStages> o{};
    index = std::clamp(index, 0, fact(kStages) - 1);
    for (int k = 0; k < kStages; ++k) { const int f = fact(kStages - 1 - k), pick = index / f; index %= f; o[static_cast<size_t>(k)] = left[static_cast<size_t>(pick)]; left.erase(left.begin() + pick); }
    return o;
}
int indexFromOrder(const std::array<int, kStages>& order) {
    std::vector<int> left = {0, 1, 2, 3, 4};
    int index = 0;
    for (int k = 0; k < kStages; ++k) {
        const auto it = std::find(left.begin(), left.end(), order[static_cast<size_t>(k)]);
        if (it == left.end()) return 0;
        index += static_cast<int>(it - left.begin()) * fact(kStages - 1 - k);
        left.erase(it);
    }
    return index;
}

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<double> steps; std::vector<std::string> labels;
        for (int i = 0; i < fact(kStages); ++i) {
            steps.push_back(i);
            const auto o = orderFromIndex(i); std::string l;
            for (int k = 0; k < kStages; ++k) { if (k) l += " > "; l += kNames[o[static_cast<size_t>(k)]]; }
            labels.push_back(l);
        }
        const std::vector<std::string> onoff = {"Off", "On"};
        std::vector<ParamSpec> v = {
            {"ms06.eq.on",        "EQ",           0, 1, 1,      Curve::Step, 1, {0, 1}, "", onoff},
            {"ms06.eq.tilt",      "Tilt",         -6, 6, 0,     Curve::Lin,  1, {}, "dB"},
            {"ms06.eq.low",       "Low shelf",    -6, 6, 0,     Curve::Lin,  1, {}, "dB"},
            {"ms06.eq.high",      "High shelf",   -6, 6, 0,     Curve::Lin,  1, {}, "dB"},
            {"ms06.eq.bell",      "Bell",         -6, 6, 0,     Curve::Lin,  1, {}, "dB"},
            {"ms06.eq.bellfreq",  "Bell freq",    200, 8000, 1000, Curve::Log, 1, {}, "Hz"},
            {"ms06.comp.on",      "Comp",         0, 1, 1,      Curve::Step, 1, {0, 1}, "", onoff},
            {"ms06.comp.thresh",  "Threshold",    -40, 0, 0,    Curve::Lin,  1, {}, "dB"},
            {"ms06.comp.ratio",   "Ratio",        1, 4, 1.5,    Curve::Lin,  1, {}, ":1"},
            {"ms06.comp.attack",  "Attack",       1, 100, 30,   Curve::Log,  1, {}, "ms"},
            {"ms06.comp.release", "Release",      20, 1000, 1000, Curve::Log, 1, {}, "ms"},
            {"ms06.comp.mix",     "Comp mix",     0, 100, 100,  Curve::Lin,  1, {}, "%"},
            {"ms06.sat.on",       "Saturate",     0, 1, 0,      Curve::Step, 1, {0, 1}, "", onoff},
            {"ms06.sat.drive",    "Drive",        0, 12, 0,     Curve::Lin,  1, {}, "dB"},
            {"ms06.sat.mix",      "Sat mix",      0, 100, 100,  Curve::Lin,  1, {}, "%"},
            {"ms06.width.on",     "Width",        0, 1, 1,      Curve::Step, 1, {0, 1}, "", onoff},
            {"ms06.width.width",  "Width amount", 0, 200, 100,  Curve::Lin,  1, {}, "%"},
            {"ms06.width.monobelow", "Mono below", 20, 300, 20, Curve::Log,  1, {}, "Hz"},
            {"ms06.limit.on",     "Limit",        0, 1, 1,      Curve::Step, 1, {0, 1}, "", onoff},
            {"ms06.limit.gain",   "Limit gain",   0, 24, 0,     Curve::Lin,  1, {}, "dB"},
            {"ms06.limit.ceiling","Ceiling",      -12, 0, -1,   Curve::Lin,  1, {}, "dBTP"},
            {"ms06.limit.release","Limit release", 1, 1000, 1000, Curve::Skew, 3, {}, "ms"},
            {"ms06.gainmatch",    "Gain match",   0, 1, 1,      Curve::Step, 1, {0, 1}, "", onoff},
            {"ms06.order",        "Order",        0, 119, 0,    Curve::Step, 1, steps, "", labels},
            {"ms06.ref",          "Reference A/B", 0, 1, 0,     Curve::Step, 1, {0, 1}, "", {"A", "B"}},
            oversampleSpec("ms06.os"),
        };
        v[CompRelease].maxLabel = "Auto"; v[LimitRelease].maxLabel = "Auto";
        v[MonoBelow].minLabel = "Off";
        v[Order].automatable = false; v[RefAB].automatable = false;
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

int Processor::latencySamples() const {
    if (target_[LimitOn] < 0.5) return 0;
    return std::max(1, static_cast<int>(std::lround(kLookaheadMs * 0.001 * fs_))) + TruePeakDetector::kTapsPerPhase;
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    outMeter_.setup(fs_, 2); for (auto& t : outTpd_) t.setup(4); resetMeters();
    for (auto& b : buf_) b.assign(kChunk, 0.0);
    for (auto& b : dry_) b.assign(kChunk, 0.0);
    const double on[kStages] = {target_[EqOn], target_[CompOn], target_[SatOn], target_[WidthOn], target_[LimitOn]};
    for (int s = 0; s < kStages; ++s) on_[static_cast<size_t>(s)].reset(fs_, 10.0, on[s]);
    compMix_.reset(fs_, 20.0, target_[CompMix] / 100.0); satMix_.reset(fs_, 20.0, target_[SatMix] / 100.0);
    for (auto& d : det_) { d.set(fs_, LevelDetector::Mode::Program); d.reset(); }
    fast_.reset(0.0); slow_.reset(0.0); compGr_ = 0;
    for (auto& d : sat_) d.prepare(fs_, target_[SatDrive] / 1.8);
    sideHp_ = {}; midLp_ = {}; midHp_ = {};
    limitActive_ = target_[LimitOn] > 0.5;
    if (limitActive_) lim_.prepare(fs_, 2, std::max(1, static_cast<int>(std::lround(kLookaheadMs * 0.001 * fs_))), true, 4);
    for (auto& tap : kw_) for (auto& k : tap) k.setup(fs_);
    ms_.fill(0.0); acc_.fill(0.0); match_.fill(0.0); gA_.fill(1.0); gB_.fill(1.0); ph_ = 0;
    msC_ = std::exp(-static_cast<double>(kRun) / (3.0 * fs_)); matchC_ = std::exp(-static_cast<double>(kRun) / (2.0 * fs_));
    order_ = orderFromIndex(static_cast<int>(target_[Order])); pendingOrder_ = static_cast<int>(target_[Order]); fade_ = 0; fadePos_ = 0;
    eqDirty_ = true;
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
}

void Processor::snapToTargets() { for (auto& s : on_) s.skip(1 << 30); compMix_.skip(1 << 30); satMix_.skip(1 << 30); updateEq(0); eqDirty_ = false; }

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case EqOn: on_[SEq].setTarget(v); break;
        case CompOn: on_[SComp].setTarget(v); break;
        case SatOn: on_[SSat].setTarget(v); break;
        case WidthOn: on_[SWidth].setTarget(v); break;
        case LimitOn: on_[SLimit].setTarget(v); break;
        case EqTilt: case EqLow: case EqHigh: case EqBell: case EqBellFreq: eqDirty_ = true; break;
        case CompMix: compMix_.setTarget(v / 100.0); break;
        case SatMix: satMix_.setTarget(v / 100.0); break;
        case SatDrive: for (auto& d : sat_) d.set(v / 1.8); break;
        case CompThresh: case CompRatio: gc_.set(target_[CompThresh], target_[CompRatio], 6.0); break;
        case CompAttack: case CompRelease: {
            const bool autoRel = target_[CompRelease] >= specs()[CompRelease].max;
            fast_.set(fs_, target_[CompAttack], autoRel ? 100.0 : target_[CompRelease]);
            slow_.set(fs_, 1000.0, 1200.0);
            break;
        }
        case MonoBelow:
            for (size_t k = 0; k < 2; ++k) {
                sideHp_[k].setup(Svf::Mode::HighPass, std::min(v, fs_ * 0.45), fs_, 0.70710678, 0);
                midHp_[k].setup(Svf::Mode::HighPass, std::min(v, fs_ * 0.45), fs_, 0.70710678, 0);
                midLp_[k].setup(Svf::Mode::LowPass, std::min(v, fs_ * 0.45), fs_, 0.70710678, 0);
            }
            break;
        case Order: pendingOrder_ = static_cast<int>(v); break;
        case Oversample: for (auto& d : sat_) d.setOversample(static_cast<int>(v)); break;   // the Saturate stage
        default: break;
    }
}

void Processor::updateEq(int ramp) {
    const double t = target_[EqTilt], f = std::min(target_[EqBellFreq], fs_ * 0.45);
    for (auto& c : eq_) {
        c.tiltLo.setupRamp(Svf::Mode::LowShelf, 1000.0, fs_, 0.5, -t, ramp);
        c.tiltHi.setupRamp(Svf::Mode::HighShelf, 1000.0, fs_, 0.5, t, ramp);
        c.low.setupRamp(Svf::Mode::LowShelf, 80.0, fs_, 0.70710678, target_[EqLow], ramp);
        c.high.setupRamp(Svf::Mode::HighShelf, std::min(12000.0, fs_ * 0.45), fs_, 0.70710678, target_[EqHigh], ramp);
        c.bell.setupRamp(Svf::Mode::Bell, f, fs_, 0.7, target_[EqBell], ramp);
    }
}

void Processor::stageEq(int nch, int n) {
    if (eqDirty_) { updateEq(kChunk); eqDirty_ = false; }
    for (int c = 0; c < nch; ++c) {
        EqCh& e = eq_[static_cast<size_t>(c)];
        for (int i = 0; i < n; ++i) { double x = buf_[static_cast<size_t>(c)][static_cast<size_t>(i)]; x = e.tiltHi.process(e.tiltLo.process(x)); x = e.high.process(e.low.process(x)); buf_[static_cast<size_t>(c)][static_cast<size_t>(i)] = e.bell.process(x); }
    }
}

void Processor::stageComp(int nch, int n) {
    const bool autoRel = target_[CompRelease] >= specs()[CompRelease].max;
    for (int i = 0; i < n; ++i) {
        double level = 0;
        for (int c = 0; c < nch; ++c) level = std::max(level, det_[static_cast<size_t>(c)].process(buf_[static_cast<size_t>(c)][static_cast<size_t>(i)]));
        const double target = gc_.gainDb(20.0 * std::log10(std::max(level, 1e-9)));
        double gr = fast_.process(target);
        if (autoRel) gr = std::min(gr, slow_.process(target));
        compGr_ = gr;
        const double g = std::pow(10.0, gr / 20.0), mix = compMix_.next();
        for (int c = 0; c < nch; ++c) { double& x = buf_[static_cast<size_t>(c)][static_cast<size_t>(i)]; x = x + mix * (x * g - x); }
    }
}

void Processor::stageSat(int nch, int n) {
    for (int i = 0; i < n; ++i) {
        const double mix = satMix_.next();
        for (int c = 0; c < nch; ++c) { sat_[static_cast<size_t>(c)].tick(); double& x = buf_[static_cast<size_t>(c)][static_cast<size_t>(i)]; x = x + mix * (sat_[static_cast<size_t>(c)].process(0, x) - x); }
    }
}

void Processor::stageWidth(int nch, int n) {
    if (nch < 2) return;
    const double w = target_[Width] / 100.0;
    const bool mono = target_[MonoBelow] > specs()[MonoBelow].min * 1.0001;
    for (int i = 0; i < n; ++i) {
        const double l = buf_[0][static_cast<size_t>(i)], r = buf_[1][static_cast<size_t>(i)];
        double m = 0.5 * (l + r);
        double s = 0.5 * (l - r) * w;
        if (mono) {   // the sides below the corner go away (LR4 high-pass); the mid takes the same crossover's all-pass so both keep their phase relation
            s = sideHp_[1].process(sideHp_[0].process(s));
            m = midLp_[1].process(midLp_[0].process(m)) + midHp_[1].process(midHp_[0].process(m));
        }
        buf_[0][static_cast<size_t>(i)] = m + s; buf_[1][static_cast<size_t>(i)] = m - s;
    }
}

void Processor::stageLimit(int nch, int n) {
    if (!limitActive_) return;
    const bool on = on_[SLimit].target() > 0.5;
    const double g = on ? std::pow(10.0, target_[LimitGain] / 20.0) : 1.0;
    const bool autoRel = target_[LimitRelease] >= specs()[LimitRelease].max;
    lim_.set(on ? target_[LimitCeiling] - 0.02 : 24.0, target_[LimitRelease], 1.0);   // Off keeps the delay but never limits
    if (autoRel) lim_.setAutoRelease(30.0, 300.0);   // short reductions recover fast (30 ms), sustained ones slowly (300 ms)
    float tmp[2][kChunk];
    for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) tmp[c][i] = static_cast<float>(buf_[static_cast<size_t>(c)][static_cast<size_t>(i)] * g);
    float* p[2] = {tmp[0], tmp[nch > 1 ? 1 : 0]};
    lim_.process(p, nch, n);
    for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) buf_[static_cast<size_t>(c)][static_cast<size_t>(i)] = tmp[c][i];
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    // the stream is worked on in runs that end at the next control boundary (every kRun samples of the stream, wherever the host's block starts) or where an order change switches:
    // what is decided at those points (gain match, fades) lands on the same samples whatever the host's block size is
    for (int off = 0; off < n;) {
        if (fade_ == 0 && pendingOrder_ != indexFromOrder(order_)) { fade_ = 1; fadePos_ = 0; }
        int m = std::min(n - off, kRun - ph_);
        if (fade_ != 0) m = std::min(m, kFade - fadePos_);
        float* p[2] = {ch[0] + off, nch > 1 ? ch[1] + off : ch[0] + off};
        runChunk(p, nch, m);
        off += m;
    }
    measureOut(ch, nch, n);
}

// the meters on what leaves the chain: short-term loudness, true peak, loudness range (a short-term value every 100 ms once 3 s have passed)
void Processor::measureOut(float** ch, int nch, int n) {
    const float* in[2] = {ch[0], ch[nch > 1 ? 1 : 0]};
    outMeter_.process(in, nch, n);
    for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) outTp_ = std::max(outTp_, outTpd_[static_cast<size_t>(c)].process(ch[c][i]));
    outShort_ = outMeter_.shortTerm();
    outSamples_ += n; sinceLra_ += n;
    const int hop = static_cast<int>(0.1 * fs_);
    while (sinceLra_ >= hop) { sinceLra_ -= hop; if (outSamples_ >= static_cast<long long>(3.0 * fs_)) lra_.add(outShort_); }
}
void Processor::resetMeters() {
    outMeter_.reset(); for (auto& t : outTpd_) t.reset(); lra_.reset();
    outShort_ = -200.0; outTp_ = 0.0; outSamples_ = 0; sinceLra_ = 0;
}

void Processor::runChunk(float** ch, int nch, int n) {
    auto power = [&](size_t tap) { double e = 0; for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) { const double y = kw_[tap][static_cast<size_t>(c)].process(buf_[static_cast<size_t>(c)][static_cast<size_t>(i)]); e += y * y; } return e; };
    for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) buf_[static_cast<size_t>(c)][static_cast<size_t>(i)] = ch[c][i];
    acc_[0] += power(0);
    for (int k = 0; k < kStages; ++k) {
        const int s = order_[static_cast<size_t>(k)];
        std::array<std::array<double, kRun>, 2> in;   // n <= kRun (process() cuts the runs)
        for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) in[static_cast<size_t>(c)][static_cast<size_t>(i)] = buf_[static_cast<size_t>(c)][static_cast<size_t>(i)];
        switch (s) { case SEq: stageEq(nch, n); break; case SComp: stageComp(nch, n); break; case SSat: stageSat(nch, n); break; case SWidth: stageWidth(nch, n); break; default: stageLimit(nch, n); break; }
        // On: crossfade between the stage and its input (the limiter keeps its delay, so it is not faded; Off just stops it limiting)
        if (s != SLimit)
            for (int i = 0; i < n; ++i) { const double on = on_[static_cast<size_t>(s)].next(); for (int c = 0; c < nch; ++c) { double& y = buf_[static_cast<size_t>(c)][static_cast<size_t>(i)]; y = in[static_cast<size_t>(c)][static_cast<size_t>(i)] + on * (y - in[static_cast<size_t>(c)][static_cast<size_t>(i)]); } }
        else for (int i = 0; i < n; ++i) on_[SLimit].next();
        // Gain match: bring this stage's output (measured before the correction) to the loudness of the chain input; the correction of the last control block ramps to the new one over this one
        const size_t tap = static_cast<size_t>(s) + 1;
        acc_[tap] += power(tap);
        const double a = gA_[static_cast<size_t>(s)], b = gB_[static_cast<size_t>(s)];
        if (a != 1.0 || b != 1.0)
            for (int i = 0; i < n; ++i) { const double gain = a + (b - a) * (ph_ + i + 1) / kRun; for (int c = 0; c < nch; ++c) buf_[static_cast<size_t>(c)][static_cast<size_t>(i)] *= gain; }
    }
    for (int c = 0; c < nch; ++c)
        for (int i = 0; i < n; ++i) {
            double y = buf_[static_cast<size_t>(c)][static_cast<size_t>(i)];
            if (fade_ == 1) y *= 1.0 - static_cast<double>(fadePos_ + i + 1) / kFade; else if (fade_ == 2) y *= static_cast<double>(fadePos_ + i + 1) / kFade;
            if (!std::isfinite(y)) y = 0.0;
            ch[c][i] = static_cast<float>(std::abs(y) < 1e-30 ? 0.0 : y);
        }
    // an order change: fading out, then the switch (the run ended here), then fading in
    if (fade_ != 0) {
        fadePos_ += n;
        if (fadePos_ >= kFade) { if (fade_ == 1) { order_ = orderFromIndex(pendingOrder_); fade_ = 2; } else fade_ = 0; fadePos_ = 0; }
    }
    ph_ += n;
    if (ph_ >= kRun) { ph_ = 0; controlBlock(); }
}

// every kRun samples of the stream: the smoothed K-weighted powers, the correction each stage needs, and the ramps for the next control block
void Processor::controlBlock() {
    const bool match = target_[GainMatch] > 0.5;
    for (size_t t = 0; t < ms_.size(); ++t) { ms_[t] = msC_ * ms_[t] + (1.0 - msC_) * acc_[t] / kRun; acc_[t] = 0.0; }
    for (size_t s = 0; s < static_cast<size_t>(kStages); ++s) {
        double want = 0.0;
        if (match && ms_[0] > 1e-10 && ms_[s + 1] > 1e-10) want = std::clamp(10.0 * std::log10(ms_[0] / ms_[s + 1]), -12.0, 12.0);
        match_[s] = want + matchC_ * (match_[s] - want);
        gA_[s] = gB_[s]; gB_[s] = std::pow(10.0, match_[s] / 20.0);
    }
}

}  // namespace sw::ms06
