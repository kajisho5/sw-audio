#include "cs04/cs04.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace sw::cs04 {

namespace { constexpr double kDeessK = 1.41421356237309505; }  // Q 0.707

namespace {
const char* kNames[kModules] = {"Gate", "EQ", "Comp", "Saturate", "De-ess", "Limit"};
constexpr int kControl = 16;
int fact(int n) { int f = 1; for (int i = 2; i <= n; ++i) f *= i; return f; }
}

std::array<int, kModules> orderFromIndex(int index) {
    std::vector<int> left = {0, 1, 2, 3, 4, 5};
    std::array<int, kModules> o{};
    index = std::clamp(index, 0, fact(kModules) - 1);
    for (int k = 0; k < kModules; ++k) {
        const int f = fact(kModules - 1 - k), pick = index / f;
        index %= f;
        o[static_cast<size_t>(k)] = left[static_cast<size_t>(pick)];
        left.erase(left.begin() + pick);
    }
    return o;
}

int indexFromOrder(const std::array<int, kModules>& order) {
    std::vector<int> left = {0, 1, 2, 3, 4, 5};
    int index = 0;
    for (int k = 0; k < kModules; ++k) {
        const auto it = std::find(left.begin(), left.end(), order[static_cast<size_t>(k)]);
        if (it == left.end()) return 0;
        index += static_cast<int>(it - left.begin()) * fact(kModules - 1 - k);
        left.erase(it);
    }
    return index;
}

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<double> steps; std::vector<std::string> labels;
        for (int i = 0; i < fact(kModules); ++i) {
            steps.push_back(i);
            const auto o = orderFromIndex(i); std::string l;
            for (int k = 0; k < kModules; ++k) { if (k) l += " > "; l += kNames[o[static_cast<size_t>(k)]]; }
            labels.push_back(l);
        }
        std::vector<ParamSpec> v = {
            {"cs04.gate.on",       "Gate",          0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"cs04.gate.thresh",   "Gate Thresh",   -80, 0, -50,  Curve::Lin,  1, {}, "dB"},
            {"cs04.gate.range",    "Gate Range",    -80, 0, -40,  Curve::Lin,  1, {}, "dB"},
            {"cs04.gate.release",  "Gate Release",  5, 2000, 100, Curve::Log,  1, {}, "ms"},
            {"cs04.eq.on",         "EQ",            0, 1, 1,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"cs04.eq.low",        "Low",           -12, 12, 0,   Curve::Lin,  1, {}, "dB"},
            {"cs04.eq.midfreq",    "Mid freq",      200, 8000, 2500, Curve::Log, 1, {}, "Hz"},
            {"cs04.eq.mid",        "Mid",           -12, 12, 0,   Curve::Lin,  1, {}, "dB"},
            {"cs04.eq.high",       "High",          -12, 12, 0,   Curve::Lin,  1, {}, "dB"},
            {"cs04.eq.out",        "EQ Output",     -12, 12, 0,   Curve::Lin,  1, {}, "dB"},
            {"cs04.comp.on",       "Comp",          0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"cs04.comp.thresh",   "Comp Thresh",   -60, 0, -20,  Curve::Lin,  1, {}, "dB"},
            {"cs04.comp.ratio",    "Comp Ratio",    1, 20, 3,     Curve::Log,  1, {}, ":1"},
            {"cs04.comp.attack",   "Comp Attack",   0.1, 100, 10, Curve::Skew, 3, {}, "ms"},
            {"cs04.comp.release",  "Comp Release",  5, 2000, 150, Curve::Skew, 3, {}, "ms"},
            {"cs04.comp.makeup",   "Comp Makeup",   0, 24, 0,     Curve::Lin,  1, {}, "dB"},
            {"cs04.sat.on",        "Saturate",      0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"cs04.sat.drive",     "Sat Drive",     0, 24, 6,     Curve::Lin,  1, {}, "dB"},
            {"cs04.sat.mix",       "Sat Mix",       0, 100, 100,  Curve::Lin,  1, {}, "%"},
            {"cs04.deess.on",      "De-ess",        0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"cs04.deess.freq",    "De-ess Freq",   2000, 12000, 6500, Curve::Log, 1, {}, "Hz"},
            {"cs04.deess.thresh",  "De-ess Thresh", -60, 0, -30,  Curve::Lin,  1, {}, "dB"},
            {"cs04.deess.range",   "De-ess Range",  -20, 0, -6,   Curve::Lin,  1, {}, "dB"},
            {"cs04.limit.on",      "Limit",         0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"cs04.limit.ceiling", "Ceiling",       -12, 0, -0.3, Curve::Lin,  1, {}, "dBFS"},
            {"cs04.limit.release", "Limit Release", 1, 1000, 50,  Curve::Log,  1, {}, "ms"},
            {"cs04.order",         "Order",         0, 719, 0,    Curve::Step, 1, steps, "", labels},
            {"cs04.lowlat",        "Low lat",       0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        };
        v[Order].automatable = false;  // spec: order is dragged and saved with presets, not automated
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

int Processor::latencySamples() const { return target_[LimitOn] > 0.5 ? (target_[LowLat] > 0.5 ? 1 : static_cast<int>(std::lround(fs_ * 0.001))) : 0; }

void Processor::prepare(double sampleRate, int maxBlock) {
    fs_ = sampleRate;
    for (auto& s : on_) s.reset(fs_, 10.0, 0.0);
    for (LinearSmoother* s : {&eqLow_, &eqMidF_, &eqMid_, &eqHigh_, &eqOut_, &makeup_, &satMix_}) s->reset(fs_, 20.0, 0.0);
    for (auto& g : gate_) g.prepare(fs_);
    for (auto& e : eq_) e = EqCh{};
    for (auto& d : compDet_) d.set(fs_, LevelDetector::Mode::Rms);
    compBall_.reset(0);
    satOs_ = {};
    sat_.setHeadroom(2.0);
    deessSvf_ = {};
    deessEnv_ = 0; deessAtk_ = Ballistics::coef(fs_, 1.0); deessRel_ = Ballistics::coef(fs_, 50.0);
    limitActive_ = target_[LimitOn] > 0.5;
    if (limitActive_) limiter_.prepare(fs_, 2, std::max(1, latencySamples()), false, 4);
    instGain_ = 1;
    dipLen_ = std::max(1, static_cast<int>(std::lround(fs_ * 0.005)));
    dipLeft_ = 0;
    for (auto& b : lim_) b.assign(static_cast<size_t>(std::max(1, maxBlock)), 0.0f);
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
    order_ = orderFromIndex(static_cast<int>(target_[Order]));
    pendingOrder_ = static_cast<int>(target_[Order]);
    snapToTargets();
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case GateOn: on_[MGate].setTarget(v); break;
        case EqOn: on_[MEq].setTarget(v); break;
        case CompOn: on_[MComp].setTarget(v); break;
        case SatOn: on_[MSat].setTarget(v); break;
        case DeessOn: on_[MDeess].setTarget(v); break;
        case LimitOn: on_[MLimit].setTarget(v); break;
        case GateThresh: case GateRange: case GateRelease:
            for (auto& g : gate_) g.set(GateEngine::Mode::Gate, target_[GateThresh], target_[GateRange], 0.1, 20.0, target_[GateRelease]);
            break;
        case EqLow: eqLow_.setTarget(v); break;
        case EqMidFreq: eqMidF_.setTarget(std::log(v)); break;
        case EqMid: eqMid_.setTarget(v); break;
        case EqHigh: eqHigh_.setTarget(v); break;
        case EqOut: eqOut_.setTarget(std::pow(10.0, v / 20.0)); break;
        case CompThresh: case CompRatio: compGc_.set(target_[CompThresh], target_[CompRatio], 0.0); break;
        case CompAttack: case CompRelease: compBall_.set(fs_, target_[CompAttack], target_[CompRelease]); break;
        case CompMakeup: makeup_.setTarget(std::pow(10.0, v / 20.0)); break;
        case SatDrive: sat_.setDriveDb(v); break;
        case SatMix: satMix_.setTarget(v / 100.0); break;
        case DeessFreq: {  // fixed-corner SVF; the gain moves sample by sample without recomputing coefficients
            const double g = std::tan(3.14159265358979323846 * std::min(v, 0.45 * fs_) / fs_);
            deessA1_ = 1.0 / (1.0 + g * (g + kDeessK)); deessA2_ = g * deessA1_; deessA3_ = g * deessA2_;
            break;
        }
        case LimitCeiling: case LimitRelease: instRel_ = Ballistics::coef(fs_, target_[LimitRelease]); break;
        case Order: { const int idx = static_cast<int>(v); if (idx != pendingOrder_) { pendingOrder_ = idx; if (dipLeft_ == 0) dipLeft_ = 2 * dipLen_; } break; }
        default: break;
    }
}

void Processor::snapToTargets() {
    for (auto& s : on_) s.skip(1 << 30);
    for (LinearSmoother* s : {&eqLow_, &eqMidF_, &eqMid_, &eqHigh_, &eqOut_, &makeup_, &satMix_}) s->skip(1 << 30);
    updateFilters(0);
}

void Processor::updateFilters(int ramp) {
    for (auto& e : eq_) {
        e.low.setupRamp(Svf::Mode::LowShelf, 100.0, fs_, 0.70710678, eqLow_.current(), ramp);
        e.mid.setupRamp(Svf::Mode::Bell, std::exp(eqMidF_.current()), fs_, 1.0, eqMid_.current(), ramp);
        e.high.setupRamp(Svf::Mode::HighShelf, std::min(10000.0, 0.45 * fs_), fs_, 0.70710678, eqHigh_.current(), ramp);
    }
}

void Processor::module(int m, double* x, int nch) {
    double y[2] = {x[0], x[1]};
    switch (m) {
        case MGate: {
            const double key = nch > 1 ? std::max(std::abs(x[0]), std::abs(x[1])) : std::abs(x[0]);
            for (int k = 0; k < nch; ++k) y[k] = x[k] * gate_[static_cast<size_t>(k)].process(key);
            break;
        }
        case MEq: {
            const double out = eqOut_.current();
            for (int k = 0; k < nch; ++k) { EqCh& e = eq_[static_cast<size_t>(k)]; y[k] = e.high.process(e.mid.process(e.low.process(x[k]))) * out; }
            break;
        }
        case MComp: {
            double lv = 0;
            for (int k = 0; k < nch; ++k) lv = std::max(lv, compDet_[static_cast<size_t>(k)].process(x[k]));
            const double gr = compBall_.process(compGc_.gainDb(20.0 * std::log10(std::max(lv, 1e-9))));
            const double g = std::pow(10.0, gr / 20.0) * makeup_.current();
            for (int k = 0; k < nch; ++k) y[k] = x[k] * g;
            break;
        }
        case MSat: {
            const double mix = satMix_.current();
            for (int k = 0; k < nch; ++k) {
                double up[2];
                satOs_[static_cast<size_t>(k)].up(x[k], up);
                up[0] = sat_.process(up[0]); up[1] = sat_.process(up[1]);
                y[k] = x[k] + mix * (satOs_[static_cast<size_t>(k)].down(up) - x[k]);
            }
            break;
        }
        case MDeess: {
            // split-free dynamic high shelf: detect on the SVF high-pass, turn the band above Freq down by up to Range
            double lp[2] = {0, 0}, bp[2] = {0, 0}, hp[2] = {0, 0}, pk = 0;
            for (int k = 0; k < nch; ++k) {
                DeessSvf& s = deessSvf_[static_cast<size_t>(k)];
                const double v3 = x[k] - s.ic2, v1 = deessA1_ * s.ic1 + deessA2_ * v3, v2 = s.ic2 + deessA2_ * s.ic1 + deessA3_ * v3;
                s.ic1 = 2.0 * v1 - s.ic1; s.ic2 = 2.0 * v2 - s.ic2;
                lp[k] = v2; bp[k] = v1; hp[k] = x[k] - kDeessK * v1 - v2;
                pk = std::max(pk, std::abs(hp[k]));
            }
            deessEnv_ = pk + (pk > deessEnv_ ? deessAtk_ : deessRel_) * (deessEnv_ - pk);
            const double over = 20.0 * std::log10(std::max(deessEnv_, 1e-9)) - target_[DeessThresh];
            const double G = over > 0 ? std::pow(10.0, std::max(target_[DeessRange], -over) / 20.0) : 1.0;
            const double sg = std::sqrt(G);
            for (int k = 0; k < nch; ++k) y[k] = G == 1.0 ? x[k] : lp[k] + kDeessK * sg * bp[k] + G * hp[k];
            break;
        }
        case MLimit:
            if (!limitActive_) {  // until the host restarts with the look-ahead: an instant, zero-latency limiter
                const double ceil = std::pow(10.0, target_[LimitCeiling] / 20.0);
                double pk = 0; for (int k = 0; k < nch; ++k) pk = std::max(pk, std::abs(x[k]));
                const double gi = pk > ceil ? ceil / pk : 1.0;
                instGain_ = gi < instGain_ ? gi : gi + instRel_ * (instGain_ - gi);
                for (int k = 0; k < nch; ++k) y[k] = x[k] * instGain_;
            }
            break;  // with look-ahead the limiter runs on the whole block after the chain (see process)
        default: break;
    }
    const double on = on_[static_cast<size_t>(m)].current();
    for (int k = 0; k < nch; ++k) x[k] = on >= 1.0 ? y[k] : x[k] + on * (y[k] - x[k]);
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    for (int start = 0; start < n; start += kControl) {
        const int len = std::min(kControl, n - start);
        bool moving = false;
        for (LinearSmoother* s : {&eqLow_, &eqMidF_, &eqMid_, &eqHigh_}) if (s->isSmoothing()) { s->skip(len); moving = true; }
        if (moving) updateFilters(len);
        for (int i = start; i < start + len; ++i) {
            for (auto& s : on_) s.next();
            eqOut_.next(); makeup_.next(); satMix_.next();
            // order change: 5 ms fade out, switch, 5 ms fade in
            double dip = 1.0;
            if (dipLeft_ > 0) {
                --dipLeft_;
                if (dipLeft_ == dipLen_) order_ = orderFromIndex(pendingOrder_);
                dip = std::abs(static_cast<double>(dipLeft_ - dipLen_)) / dipLen_;
            }
            double x[2] = {ch[0][i], nch > 1 ? ch[1][i] : 0.0};
            for (int m : order_) {
                if (m == MLimit && limitActive_) { for (int k = 0; k < nch; ++k) lim_[k][static_cast<size_t>(i)] = static_cast<float>(x[k]); continue; }
                module(m, x, nch);
            }
            for (int k = 0; k < nch; ++k) { const double y = x[k] * dip; ch[k][i] = static_cast<float>(std::abs(y) < 1e-30 ? 0.0 : y); }
        }
    }
    if (limitActive_) {
        // look-ahead limiter: in the chain position of Limit only the modules after it differ;
        // it runs on the chain output (Limit is placed last in practice; when it is not, the modules after it see the
        // undelayed signal -- documented). Off = ceiling raised far above full scale (keeps the latency constant).
        const double ceil = target_[LimitOn] > 0.5 ? target_[LimitCeiling] : 24.0;
        limiter_.set(ceil - 0.0, target_[LimitRelease], 1.0);
        limiter_.process(ch, nch, n);
    }
}

}  // namespace sw::cs04
