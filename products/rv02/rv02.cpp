#include "rv02/rv02.hpp"
#include "sw/tail.hpp"
#include <algorithm>
#include <cmath>

namespace sw::rv02 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"rv02.decay",    "Decay",     0.5, 6, 2.0,  Curve::Log, 1, {}, "s"},
            {"rv02.predelay", "Pre-delay", 0, 200, 20,   Curve::Skew, 2, {}, "ms"},
            {"rv02.damping",  "Damping",   0, 100, 50,   Curve::Lin, 1, {}, "%"},
            {"rv02.lowcut",   "Low cut",   20, 500, 80,  Curve::Log, 1, {}, "Hz"},
            {"rv02.width",    "Width",     0, 100, 100,  Curve::Lin, 1, {}, "%"},
            {"rv02.mix",      "Mix",       0, 100, 30,   Curve::Lin, 1, {}, "%"},
            {"rv02.monoin",   "Mono in",   0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"rv02.sync",     "Pre-delay sync", 0, 4, 0, Curve::Step, 1, {0, 1, 2, 3, 4}, "", {"Off", "1/32", "1/16", "1/8", "1/4"}},
            {"rv02.evo.on",   "Duck",      0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            unitSpec("rv02.unit"),
        };
        v[Damping].minLabel = "Dark"; v[Damping].maxLabel = "Bright"; v[Width].minLabel = "Mono"; v[Width].maxLabel = "Wide";
        return v;
    }();
    return s;
}
double dispersionDelay(double f, double fs) {
    const double w = 2.0 * 3.14159265358979323846 * f / fs, a = -0.7;
    return 48.0 * (1.0 - a * a) / (1.0 + 2.0 * a * std::cos(w) + a * a);
}

namespace {
constexpr double kPlateMinMs = 9.0, kPlateMaxMs = 36.0, kModDepth = 5.0, kModRate = 0.5;
constexpr double kDuckDb = -6.0;
double dampingHz(double pct) { return 2000.0 * std::pow(7.0, pct * 0.01); }   // Dark 2 kHz .. Bright 14 kHz (50 % = 5.3 kHz)
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

double Processor::preDelayMs() const {
    if (target_[Sync] > 0.5 && bpm_ > 0.0) {
        static const double note[5] = {0.0, 0.125, 0.25, 0.5, 1.0};   // fraction of a beat: 1/32 .. 1/4 of a whole note at the host tempo
        return std::min(200.0, 60000.0 / bpm_ * note[static_cast<int>(target_[Sync] + 0.5)]);
    }
    return target_[PreDelay];
}

void Processor::updateLines() {
    const int n = fdn_.lines();
    for (int i = 0; i < n; ++i) fdn_.setLength(i, kPlateMinMs * std::pow(kPlateMaxMs / kPlateMinMs, static_cast<double>(i) / (n - 1)) * 0.001 * fs_);
    fdn_.setModulation(kModDepth, kModRate);
    fdn_.setDecay(target_[Decay]);
    fdn_.setDamping(dampingHz(target_[Damping]));
}

void Processor::updateFilters() { for (auto& f : hp_) f.setup(Svf::Mode::HighPass, target_[LowCut], fs_, 0.70710678, 0); }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    fdn_.prepare(fs_, 16, 0.1);
    for (auto& b : pre_) b.assign(static_cast<size_t>(0.25 * fs_) + 8, 0.0f);
    for (auto& c : chain_) c = Chain{};
    prePos_ = 0; preLen_ = preDelayMs() * 0.001 * fs_; env_ = 0.0; duckGain_ = 1.0;
    width_.reset(fs_, 20.0, target_[Width] * 0.01);
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
        case Decay: case Damping: updateLines(); break;
        case LowCut: updateFilters(); break;
        case Width: width_.setTarget(v * 0.01); break;
        default: break;
    }
}

void Processor::snapToTargets() {
    if (!prepared_) return;
    updateLines(); fdn_.snapLengths(); updateFilters();
    lateTrim_ = 1.0 / std::sqrt(Fdn::kEnergyConstant * target_[Decay] / fdn_.meanLengthSeconds());
    preLen_ = preDelayMs() * 0.001 * fs_;
    width_.skip(1 << 30);
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const bool mono = target_[MonoIn] > 0.5 || nch < 2, duck = target_[DuckOn] > 0.5;
    const double envA = 1.0 - std::exp(-1.0 / (0.005 * fs_)), envR = 1.0 - std::exp(-1.0 / (0.15 * fs_));
    const double dgA = 1.0 - std::exp(-1.0 / (0.01 * fs_)), dgR = 1.0 - std::exp(-1.0 / (0.25 * fs_));
    const size_t preSz = pre_[0].size();
    for (int i = 0; i < n; ++i) {
        const double w = width_.next();
        lateTrim_ += 0.0005 * (1.0 / std::sqrt(Fdn::kEnergyConstant * target_[Decay] / fdn_.meanLengthSeconds()) - lateTrim_);
        const double l0 = ch[0][i], r0 = nch > 1 ? ch[1][i] : l0, m = 0.5 * (l0 + r0);
        const double am = std::abs(m);
        env_ += (am > env_ ? envA : envR) * (am - env_);
        const double k = std::clamp((20.0 * std::log10(env_ + 1e-9) + 40.0) / 30.0, 0.0, 1.0);
        const double dgTarget = duck ? std::pow(10.0, kDuckDb * k / 20.0) : 1.0;
        duckGain_ += (dgTarget < duckGain_ ? dgA : dgR) * (dgTarget - duckGain_);
        preLen_ += std::clamp(preDelayMs() * 0.001 * fs_ - preLen_, -1.0, 1.0);
        const double in[2] = {mono ? m : l0, mono ? m : r0};
        double x[2];
        for (int c = 0; c < (mono ? 1 : 2); ++c) {
            auto& b = pre_[static_cast<size_t>(c)];
            b[prePos_] = static_cast<float>(in[c]);
            double rp = static_cast<double>(prePos_) - preLen_; if (rp < 0) rp += static_cast<double>(preSz);
            const size_t i0 = static_cast<size_t>(rp) % preSz, i1 = (i0 + 1) % preSz; const double fr = rp - std::floor(rp);
            x[c] = chain_[static_cast<size_t>(c)].process(b[i0] + fr * (b[i1] - b[i0]));
        }
        prePos_ = (prePos_ + 1) % preSz;
        double fl, fr2;
        if (mono) fdn_.process(x[0], fl, fr2); else fdn_.processStereo(x[0], x[1], fl, fr2);
        double l = fl * lateTrim_, r = fr2 * lateTrim_;
        const double mid = 0.5 * (l + r), side = 0.5 * (l - r) * w;
        l = hp_[0].process(mid + side) * duckGain_; r = hp_[1].process(mid - side) * duckGain_;
        if (std::abs(l) < 1e-30) l = 0.0; if (std::abs(r) < 1e-30) r = 0.0;
        ch[0][i] = static_cast<float>(l);
        if (nch > 1) ch[1][i] = static_cast<float>(r);
    }
}

// the tail: Decay is the time to fall by 60 dB, taken down to -80 dB, after the pre-delay
double Processor::tailSeconds() const { return tail::fromRt60(target_[Decay]) + target_[PreDelay] * 0.001 + 0.4; }

}  // namespace sw::rv02
