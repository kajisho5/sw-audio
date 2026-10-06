#include "rv01/rv01.hpp"
#include <algorithm>
#include <cmath>

namespace sw::rv01 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"rv01.algorithm", "Algorithm", 0, 4, 0,      Curve::Step, 1, {0, 1, 2, 3, 4}, "", {"Hall", "Room", "Chamber", "Plate", "Ambience"}},
        {"rv01.predelay",  "Pre-delay", 0, 500, 24,   Curve::Skew, 2, {}, "ms"},
        {"rv01.size",      "Size",      0, 100, 74,   Curve::Lin, 1, {}, "%"},
        {"rv01.decay",     "Decay",     0.2, 20, 2.8, Curve::Log, 1, {}, "s"},
        {"rv01.diffusion", "Diffusion", 0, 100, 82,   Curve::Lin, 1, {}, "%"},
        {"rv01.damping",   "Damping",   1000, 20000, 6500, Curve::Log, 1, {}, "Hz"},
        {"rv01.lowcut",    "Low cut",   20, 1000, 120, Curve::Log, 1, {}, "Hz"},
        {"rv01.highcut",   "High cut",  1000, 20000, 9000, Curve::Log, 1, {}, "Hz"},
        {"rv01.erlate",    "ER / late", 0, 100, 40,   Curve::Lin, 1, {}, ""},
        {"rv01.width",     "Width",     0, 150, 100,  Curve::Lin, 1, {}, "%"},
        {"rv01.mix",       "Mix",       0, 100, 22,   Curve::Lin, 1, {}, "%"},
        {"rv01.freeze",    "Freeze",    0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"rv01.monolowend","Mono low end", 0, 1, 0,   Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"rv01.duck",      "Duck",      -18, 0, -6,   Curve::Lin, 1, {}, "dB"},
    };
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846;
struct AlgoDesign { double minMs, maxMs, modDepth, modRate, dampTilt; };
constexpr AlgoDesign kAlgo[5] = {{24.0, 80.0, 6.0, 0.30, 1.0},    // Hall
                                 {8.0, 29.0, 2.0, 0.40, 1.0},     // Room
                                 {15.0, 55.0, 3.0, 0.35, 1.0},    // Chamber
                                 {10.0, 40.0, 5.0, 0.50, 1.5},    // Plate
                                 {4.0, 16.0, 1.5, 0.40, 1.2}};    // Ambience
struct TapDesign { double ms, gain, pan; };
constexpr TapDesign kTaps[5][10] = {
    {{17, .9, -.5}, {29, .75, .6}, {41, .7, -.8}, {53, .6, .8}, {67, .5, -.3}, {79, .45, .4}, {95, .4, -.9}, {112, .3, .9}, {0, 0, 0}, {0, 0, 0}},
    {{4, .9, -.6}, {7, .85, .7}, {11, .8, -.4}, {14, .7, .5}, {19, .65, -.8}, {23, .55, .8}, {28, .5, -.2}, {33, .45, .3}, {38, .4, -.7}, {44, .3, .6}},
    {{9, .85, -.4}, {16, .8, .5}, {24, .7, -.7}, {33, .65, .7}, {43, .55, -.2}, {55, .5, .3}, {68, .4, -.8}, {83, .3, .8}, {0, 0, 0}, {0, 0, 0}},
    {{3, .8, -.3}, {6, .7, .3}, {10, .6, -.5}, {15, .5, .5}, {22, .4, -.2}, {30, .3, .2}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{2, .9, -.7}, {3.5, .85, .7}, {5, .8, -.4}, {7, .75, .5}, {9.5, .65, -.8}, {12, .6, .8}, {15, .5, -.3}, {18, .45, .4}, {22, .35, -.6}, {27, .3, .6}}};
constexpr double kApMs[4] = {3.0, 2.2, 7.9, 5.8};
double sizeScale(double sizePct) { return 0.4 + 1.2 * sizePct * 0.01; }
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::updateLines() {
    const int alg = static_cast<int>(target_[Algorithm] + 0.5);
    const AlgoDesign& a = kAlgo[alg];
    const double sc = sizeScale(target_[Size]);
    const int n = fdn_.lines();
    for (int i = 0; i < n; ++i) {
        const double ms = a.minMs * std::pow(a.maxMs / a.minMs, static_cast<double>(i) / (n - 1)) * sc;
        fdn_.setLength(i, ms * 0.001 * fs_);
    }
    fdn_.setModulation(a.modDepth, a.modRate);
    fdn_.setDecay(target_[Decay]);
    fdn_.setDamping(target_[Damping] * a.dampTilt);
    fdn_.setFreeze(target_[Freeze] > 0.5);
}

void Processor::updateFilters() {
    for (int c = 0; c < 2; ++c) {
        hp_[static_cast<size_t>(c)].setup(Svf::Mode::HighPass, target_[LowCut], fs_, 0.70710678, 0);
        lp_[static_cast<size_t>(c)].setup(Svf::Mode::LowPass, std::min(target_[HighCut], 0.45 * fs_), fs_, 0.70710678, 0);
    }
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    fdn_.prepare(fs_, 16, 0.2);   // longest line: 80 ms x 1.6 = 128 ms (+ modulation)
    pre_.assign(static_cast<size_t>(0.5 * fs_) + 8, 0.0f);
    erBuf_.assign(static_cast<size_t>(0.25 * fs_) + 8, 0.0f);
    for (int k = 0; k < 4; ++k) { ap_[static_cast<size_t>(k)].len = std::max(1, static_cast<int>(std::lround(kApMs[k] * 0.001 * fs_))); ap_[static_cast<size_t>(k)].buf.assign(static_cast<size_t>(ap_[static_cast<size_t>(k)].len), 0.0f); ap_[static_cast<size_t>(k)].pos = 0; }
    prePos_ = erPos_ = 0; preLen_ = target_[PreDelay] * 0.001 * fs_; env_ = 0.0; duckGain_ = 1.0;
    for (auto& g : tapGain_) g.fill(0.0);
    width_.reset(fs_, 20.0, target_[Width] * 0.01);
    const double er = target_[ErLate] * 0.01;
    erG_.reset(fs_, 20.0, std::sqrt(er)); lateG_.reset(fs_, 20.0, std::sqrt(std::max(0.0, 1.0 - er)));   // (max: 100 * 0.01 is 1.0000000000000000208 when the compiler fuses the multiply into the subtraction, as on arm64)
    for (auto& f : sideHp_) f.setup(Svf::Mode::HighPass, 120.0, fs_, 0.70710678, 0);
    updateLines(); fdn_.snapLengths(); updateFilters();
    lateTrim_ = 1.0 / std::sqrt(Fdn::kEnergyConstant * target_[Decay] / fdn_.meanLengthSeconds());
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (!prepared_) return;
    switch (id) {
        case Algorithm: case Size: case Decay: case Damping: case Freeze: updateLines(); break;
        case LowCut: case HighCut: updateFilters(); break;
        case Width: width_.setTarget(v * 0.01); break;
        case ErLate: erG_.setTarget(std::sqrt(v * 0.01)); lateG_.setTarget(std::sqrt(std::max(0.0, 1.0 - v * 0.01))); break;
        default: break;
    }
}

void Processor::snapToTargets() {
    if (!prepared_) return;
    updateLines(); fdn_.snapLengths(); updateFilters();
    lateTrim_ = 1.0 / std::sqrt(Fdn::kEnergyConstant * target_[Decay] / fdn_.meanLengthSeconds());
    preLen_ = target_[PreDelay] * 0.001 * fs_;
    for (LinearSmoother* s : {&width_, &erG_, &lateG_}) s->skip(1 << 30);
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const int alg = static_cast<int>(target_[Algorithm] + 0.5);
    const double sc = sizeScale(target_[Size]);
    const double apG = 0.2 + 0.5 * target_[Diffusion] * 0.01;
    const bool monoLow = target_[MonoLow] > 0.5, frozen = target_[Freeze] > 0.5;
    const double duckDb = target_[Duck];
    const double envA = 1.0 - std::exp(-1.0 / (0.005 * fs_)), envR = 1.0 - std::exp(-1.0 / (0.15 * fs_));
    const double dgA = 1.0 - std::exp(-1.0 / (0.01 * fs_)), dgR = 1.0 - std::exp(-1.0 / (0.25 * fs_));
    const size_t preSz = pre_.size(), erSz = erBuf_.size();
    std::array<double, 12> tapT{}, tapL{}, tapR{};
    double nl = 0, nr = 0;
    for (int k = 0; k < 10; ++k) {
        const TapDesign& t = kTaps[alg][k];
        tapT[static_cast<size_t>(k)] = t.ms * 0.001 * fs_ * sc;
        const double ang = (t.pan + 1.0) * 0.25 * kPi;   // constant-power pan
        tapL[static_cast<size_t>(k)] = t.gain > 0 ? t.gain * std::cos(ang) : 0.0; tapR[static_cast<size_t>(k)] = t.gain > 0 ? t.gain * std::sin(ang) : 0.0;
        nl += tapL[static_cast<size_t>(k)] * tapL[static_cast<size_t>(k)]; nr += tapR[static_cast<size_t>(k)] * tapR[static_cast<size_t>(k)];
    }
    for (int k = 0; k < 10; ++k) { tapL[static_cast<size_t>(k)] /= std::sqrt(nl); tapR[static_cast<size_t>(k)] /= std::sqrt(nr); }   // unit energy per side
    const double lateTrimTarget = 1.0 / std::sqrt(Fdn::kEnergyConstant * target_[Decay] / fdn_.meanLengthSeconds());
    for (int i = 0; i < n; ++i) {
        const double eg = erG_.next(), lg = lateG_.next(), w = width_.next();
        lateTrim_ += 0.0005 * (lateTrimTarget - lateTrim_);
        const double m = nch > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i];
        // duck: the dry level (peak follower) -> wet gain; full effect from -10 dBFS, none under -40 dBFS
        const double am = std::abs(m);
        env_ += (am > env_ ? envA : envR) * (am - env_);
        const double envDb = 20.0 * std::log10(env_ + 1e-9);
        const double k = std::clamp((envDb + 40.0) / 30.0, 0.0, 1.0);
        const double dgTarget = std::pow(10.0, duckDb * k / 20.0);
        duckGain_ += (dgTarget < duckGain_ ? dgA : dgR) * (dgTarget - duckGain_);
        // pre-delay (gliding length, linear interpolation)
        const double tl = target_[PreDelay] * 0.001 * fs_;
        preLen_ += std::clamp(tl - preLen_, -1.0, 1.0);
        pre_[prePos_] = static_cast<float>(m);
        double rp = static_cast<double>(prePos_) - preLen_; if (rp < 0) rp += static_cast<double>(preSz);
        const size_t i0 = static_cast<size_t>(rp) % preSz, i1 = (i0 + 1) % preSz; const double fr = rp - std::floor(rp);
        const double xp = pre_[i0] + fr * (pre_[i1] - pre_[i0]);
        prePos_ = (prePos_ + 1) % preSz;
        // early reflections
        erBuf_[erPos_] = static_cast<float>(xp);
        double erL = 0, erR = 0;
        for (int t = 0; t < 10; ++t) {
            if (tapL[static_cast<size_t>(t)] == 0.0 && tapR[static_cast<size_t>(t)] == 0.0) continue;
            const double d = std::min(tapT[static_cast<size_t>(t)], static_cast<double>(erSz - 2));
            double rq = static_cast<double>(erPos_) - d; if (rq < 0) rq += static_cast<double>(erSz);
            const size_t j0 = static_cast<size_t>(rq) % erSz, j1 = (j0 + 1) % erSz; const double f2 = rq - std::floor(rq);
            const double v = erBuf_[j0] + f2 * (erBuf_[j1] - erBuf_[j0]);
            erL += tapL[static_cast<size_t>(t)] * v; erR += tapR[static_cast<size_t>(t)] * v;
        }
        erPos_ = (erPos_ + 1) % erSz;
        // late: diffusers into the FDN
        double d = frozen ? 0.0 : xp;
        for (auto& a : ap_) d = a.process(d, apG);
        double fl, fr2; fdn_.process(d, fl, fr2);
        double l = eg * erL * (frozen ? 0.0 : 1.0) + lg * lateTrim_ * fl, r = eg * erR * (frozen ? 0.0 : 1.0) + lg * lateTrim_ * fr2;
        // width (M/S), mono low end
        const double mid = 0.5 * (l + r); double side = 0.5 * (l - r) * w;
        if (monoLow) side = sideHp_[1].process(sideHp_[0].process(side));
        l = mid + side; r = mid - side;
        l = lp_[0].process(hp_[0].process(l)) * duckGain_; r = lp_[1].process(hp_[1].process(r)) * duckGain_;
        if (std::abs(l) < 1e-30) l = 0.0; if (std::abs(r) < 1e-30) r = 0.0;
        ch[0][i] = static_cast<float>(l);
        if (nch > 1) ch[1][i] = static_cast<float>(r);
    }
}

}  // namespace sw::rv01
