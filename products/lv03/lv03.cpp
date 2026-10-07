#include "lv03/lv03.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv03 {
namespace {
// Handheld = the screen's standing state (the spec's defaults); the others are design values
//                    Trim HPF Gate  Range  Low  Mid  High MidF  FB  Th   Ratio DsAmt DsF   Phase
struct MicSet { double trim, hpf, thr, range, low, mid, high, midF, fb, cth, cr, ds, dsF; };
constexpr MicSet kMic[4] = {
    {6, 80, -42, -30, -2, 2, 1.5, 1200, 1, -18, 3.0, 4, 6500},    // Handheld
    {14, 100, -45, -24, -3, 1, 3.0, 2500, 1, -20, 3.5, 5, 6800},  // Lavalier
    {10, 90, -44, -28, -2, 1.5, 2.0, 1800, 1, -18, 3.0, 5, 6500}, // Headset
    {18, 120, -48, -20, -3, 2.5, 1.0, 2000, 1, -22, 2.5, 3, 7000},// Podium
};
}

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"lv03.mic",     "Mic",           0, 3, 0,      Curve::Step, 1, {0, 1, 2, 3}, "", {"Handheld", "Lavalier", "Headset", "Podium"}},
            {"lv03.trim",    "Trim",          -20, 40, 6,   Curve::Lin, 1, {}, "dB"},
            {"lv03.hpf",     "HPF",           20, 400, 80,  Curve::Log, 1, {}, "Hz"},
            {"lv03.phase",   "Ø",             0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"lv03.gate.th", "Gate Thresh",   -80, 0, -42,  Curve::Lin, 1, {}, "dB"},
            {"lv03.gate.rg", "Gate Range",    -80, 0, -30,  Curve::Lin, 1, {}, "dB"},
            {"lv03.eq.low",  "EQ Low",        -12, 12, -2,  Curve::Lin, 1, {}, "dB"},
            {"lv03.eq.mid",  "EQ Mid",        -12, 12, 2,   Curve::Lin, 1, {}, "dB"},
            {"lv03.eq.high", "EQ High",       -12, 12, 1.5, Curve::Lin, 1, {}, "dB"},
            {"lv03.eq.midf", "EQ Mid f",      200, 8000, 1200, Curve::Log, 1, {}, "Hz"},
            {"lv03.fb",      "Feedback guard", 0, 1, 1,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"lv03.comp.th", "Comp Thresh",   -40, 0, -18,  Curve::Lin, 1, {}, "dB"},
            {"lv03.comp.r",  "Comp Ratio",    1, 10, 3,     Curve::Log, 1, {}, ":1"},
            {"lv03.deess.a", "De-ess Amount", 0, 12, 4,     Curve::Lin, 1, {}, "dB"},
            {"lv03.deess.f", "De-ess Freq",   3000, 12000, 6500, Curve::Log, 1, {}, "Hz"},
            {"lv03.out",     "Out",           -20, 10, -2,  Curve::Lin, 1, {}, "dB"},
        };
        v[Hpf].minLabel = "Off";
        return v;
    }();
    return s;
}

std::vector<std::pair<int, double>> micPreset(int m) {
    const MicSet& k = kMic[std::clamp(m, 0, 3)];
    return {{Trim, k.trim}, {Hpf, k.hpf}, {Phase, 0}, {GateThresh, k.thr}, {GateRange, k.range}, {EqLow, k.low}, {EqMid, k.mid}, {EqHigh, k.high}, {EqMidF, k.midF},
            {FbGuard, k.fb}, {CompThresh, k.cth}, {CompRatio, k.cr}, {DeessAmount, k.ds}, {DeessFreq, k.dsF}};
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate; guard_.prepare(fs_, 4); guard_.setParams(1, -12, 0.1, 8.0);
    gate_.prepare(fs_); det_.set(fs_, LevelDetector::Mode::Program); det_.reset(); comp_.set(fs_, 10.0, 120.0); comp_.reset(0.0);
    dsA_ = Ballistics::coef(fs_, 1.0); dsR_ = Ballistics::coef(fs_, 40.0); dsEnv_ = 0; compDb_ = deessDb_ = 0;
    for (auto& c : c_) { c.hp.reset(); c.low.reset(); c.mid.reset(); c.high.reset(); c.ds.reset(); }
    prepared_ = true; writes_.clear(); updateFilters(); trim_ = std::pow(10.0, target_[Trim] / 20.0);
}

void Processor::updateFilters() {
    for (auto& c : c_) {
        c.hp.setup(Svf::Mode::HighPass, std::max(20.0, target_[Hpf]), fs_, 0.70710678, 0);
        c.low.setup(Svf::Mode::LowShelf, 100.0, fs_, 0.70710678, target_[EqLow]);
        c.mid.setup(Svf::Mode::Bell, target_[EqMidF], fs_, 1.0, target_[EqMid]);
        c.high.setup(Svf::Mode::HighShelf, std::min(8000.0, fs_ * 0.4), fs_, 0.70710678, target_[EqHigh]);
        c.ds.setup(Svf::Mode::HighPass, std::min(target_[DeessFreq], fs_ * 0.45), fs_, 0.70710678, 0);
    }
    gate_.set(GateEngine::Mode::Gate, target_[GateThresh], target_[GateRange], 1.0, 100.0, 200.0);
    gc_.set(target_[CompThresh], target_[CompRatio], 6.0);
}

void Processor::applyParam(int id, double v) {
    target_[static_cast<size_t>(id)] = v;
    if (!prepared_) return;
    if (id == Trim) trim_ = std::pow(10.0, v / 20.0);
    else if (id != Mic && id != Phase && id != DeessAmount && id != Out) updateFilters();
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    applyParam(id, v);
    if (id != Mic) writes_.erase(std::remove_if(writes_.begin(), writes_.end(), [id](const std::pair<int, double>& w) { return w.first == id; }), writes_.end());
    else { const auto p = micPreset(static_cast<int>(v + 0.5)); for (const auto& w : p) applyParam(w.first, w.second); writes_ = p; }
}

int Processor::takeParamWrite(int& id, double& plain) {
    if (writes_.empty()) return 0;
    id = writes_.front().first; plain = writes_.front().second; writes_.erase(writes_.begin());
    return 7;
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    const int nc = std::min(numCh, 2);
    const double sign = target_[Phase] > 0.5 ? -1.0 : 1.0, g = trim_ * sign;
    const bool hpOn = target_[Hpf] > 20.0 * 1.0001;
    for (int i = 0; i < n; ++i) {
        double y[2] = {0, 0}, key = 0;
        for (int c = 0; c < nc; ++c) { Chan& s = c_[static_cast<size_t>(c)]; double v = ch[c][i] * g; if (hpOn) v = s.hp.process(v); y[c] = v; key = std::max(key, std::abs(v)); }
        const double gg = gate_.process(key);
        for (int c = 0; c < nc; ++c) { Chan& s = c_[static_cast<size_t>(c)]; ch[c][i] = static_cast<float>(s.high.process(s.mid.process(s.low.process(y[c] * gg)))); }
    }
    if (target_[FbGuard] > 0.5) guard_.process(ch, nc, n);
    const double dsAmt = target_[DeessAmount], dsThr = std::pow(10.0, -34.0 / 20.0);
    for (int i = 0; i < n; ++i) {
        double x[2] = {ch[0][i], nc > 1 ? ch[1][i] : 0.0}, key = std::max(std::abs(x[0]), std::abs(x[1]));
        const double lv = det_.process(key), ldb = 20.0 * std::log10(std::max(lv, 1e-9));
        compDb_ = comp_.process(gc_.gainDb(ldb)); const double cg = std::pow(10.0, compDb_ / 20.0);
        double hpv[2] = {0, 0}, band = 0;
        for (int c = 0; c < nc; ++c) { hpv[c] = c_[static_cast<size_t>(c)].ds.process(x[c] * cg); band = std::max(band, std::abs(hpv[c])); }
        dsEnv_ = band > dsEnv_ ? dsA_ * dsEnv_ + (1 - dsA_) * band : dsR_ * dsEnv_ + (1 - dsR_) * band;
        const double over = dsEnv_ > dsThr ? 20.0 * std::log10(dsEnv_ / dsThr) : 0.0;
        deessDb_ = -std::min(dsAmt, 0.8 * over);
        if (std::abs(deessDb_ - shelfDb_) > 0.05 && (++dsTick_ & 15) == 0) { shelfDb_ = deessDb_; for (auto& s : c_) s.dsShelf.setup(Svf::Mode::HighShelf, std::min(0.7 * target_[DeessFreq], fs_ * 0.45), fs_, 0.70710678, shelfDb_); }
        for (int c = 0; c < nc; ++c) { const double o = shelfDb_ < -0.05 ? c_[static_cast<size_t>(c)].dsShelf.process(x[c] * cg) : (c_[static_cast<size_t>(c)].dsShelf.process(0.0), x[c] * cg); const float f = static_cast<float>(o); ch[c][i] = std::abs(f) < 1e-30f ? 0.0f : f; }
    }
}

}  // namespace sw::lv03
