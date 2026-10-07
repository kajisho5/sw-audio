#include "lv13/lv13.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace sw::lv13 {
namespace {
constexpr double kDefFreq[kBands] = {100, 250, 630, 1600, 4000, 10000};
}

const std::vector<ParamSpec>& specs() {
    static std::vector<std::string> names, ids;
    static const std::vector<ParamSpec> s = [] {
        names.reserve(4 * kBands); ids.reserve(4 * kBands);
        std::vector<ParamSpec> v;
        for (int b = 0; b < kBands; ++b) {
            const std::string n = "Band " + std::to_string(b + 1), key = "lv13.b" + std::to_string(b + 1);
            names.push_back(n + " Type");  ids.push_back(key + ".type");  v.push_back({ids.back().c_str(), names.back().c_str(), 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Bell", "Shelf"}});
            names.push_back(n + " Freq");  ids.push_back(key + ".freq");  v.push_back({ids.back().c_str(), names.back().c_str(), 20, 20000, kDefFreq[b], Curve::Log, 1, {}, "Hz"});
            names.push_back(n + " Gain");  ids.push_back(key + ".gain");  v.push_back({ids.back().c_str(), names.back().c_str(), -15, 15, 0, Curve::Lin, 1, {}, "dB"});
            names.push_back(n + " Q");     ids.push_back(key + ".q");     v.push_back({ids.back().c_str(), names.back().c_str(), 0.3, 10, 2.0, Curve::Log, 1, {}, ""});
        }
        v.push_back({"lv13.hpf", "HPF", 20, 400, 90, Curve::Log, 1, {}, "Hz"});
        v.push_back({"lv13.lpf", "LPF", 5000, 20000, 18000, Curve::Log, 1, {}, "Hz"});
        v[Hpf].minLabel = "Off"; v[Lpf].maxLabel = "Off"; v[Lpf].maxLabelNorm = 0.98;
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int maxBlock) {
    fs_ = sampleRate; rta_.setup(fs_, 4096, 1.0);
    for (auto& c : c_) { for (auto& f : c.band) f.reset(); c.hp.reset(); c.lp.reset(); }
    on_.fill(false); mono_.assign(static_cast<size_t>(std::max(1, maxBlock) + 8), 0.0f); prepared_ = true; updateAll(0);
}

void Processor::updateBand(int b, int ramp) {
    const double f = std::min(target_[static_cast<size_t>(id(b, Freq))], fs_ * 0.45), g = target_[static_cast<size_t>(id(b, Gain))], q = target_[static_cast<size_t>(id(b, Q))];
    const bool shelf = target_[static_cast<size_t>(id(b, Type))] > 0.5;
    const Svf::Mode m = shelf ? (f < 1000.0 ? Svf::Mode::LowShelf : Svf::Mode::HighShelf) : Svf::Mode::Bell;
    const bool on = std::abs(g) > 0.005; const size_t k = static_cast<size_t>(b);
    for (auto& c : c_) {
        if (on) { if (!on_[k] || ramp <= 1) c.band[k].setup(m, f, fs_, q, g); else c.band[k].setupRamp(m, f, fs_, q, g, ramp); }
        else if (on_[k] && ramp > 1) c.band[k].setupRamp(m, f, fs_, q, 0.0, ramp);
    }
    on_[k] = on || (on_[k] && ramp > 1);
}
void Processor::updateAll(int ramp) {
    for (int b = 0; b < kBands; ++b) updateBand(b, ramp);
    for (auto& c : c_) { c.hp.setup(Svf::Mode::HighPass, std::max(20.0, target_[Hpf]), fs_, 0.70710678, 0); c.lp.setup(Svf::Mode::LowPass, std::min(target_[Lpf], fs_ * 0.45), fs_, 0.70710678, 0); }
    hpOn_ = target_[Hpf] > 20.0 * 1.0001; lpOn_ = target_[Lpf] < 20000.0 * 0.9999;
}
void Processor::setParam(int id_, double v) {
    const auto& sp = specs()[static_cast<size_t>(id_)];
    target_[static_cast<size_t>(id_)] = sp.toValue(sp.toNorm(v));
    if (!prepared_) return;
    if (id_ < Hpf) updateBand(id_ / kPerBand, 256); else updateAll(0);
}

std::vector<Suggestion> Processor::suggestions() const {
    std::vector<Suggestion> v;
    if (rta_.frames() < 4) return v;
    std::vector<std::pair<double, int>> cand;
    for (int b = 2; b < ThirdOctaveAnalyzer::kBands - 2; ++b) {
        const double l = rta_.levelDb(b); if (l < -90.0) continue;
        double sum = 0; int cnt = 0; for (int d : {-2, -1, 1, 2}) { const double o = rta_.levelDb(b + d); if (o > -150.0) { sum += o; ++cnt; } }
        if (!cnt) continue;
        const double excess = l - sum / cnt; if (excess >= 6.0) cand.push_back({excess, b});
    }
    std::sort(cand.begin(), cand.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t i = 0; i < cand.size() && v.size() < 3; ++i) v.push_back({ThirdOctaveAnalyzer::centerHz(cand[i].second), -std::min(6.0, 0.5 * cand[i].first), 4.0});
    return v;
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    const int nc = std::min(numCh, 2);
    if (mono_.size() < static_cast<size_t>(n)) mono_.assign(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i) mono_[static_cast<size_t>(i)] = nc > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
    rta_.process(mono_.data(), n);
    for (int c = 0; c < nc; ++c) {
        Chan& s = c_[static_cast<size_t>(c)]; float* x = ch[c];
        for (int i = 0; i < n; ++i) {
            double y = x[i];
            if (hpOn_) y = s.hp.process(y);
            for (int b = 0; b < kBands; ++b) if (on_[static_cast<size_t>(b)]) y = s.band[static_cast<size_t>(b)].process(y);
            if (lpOn_) y = s.lp.process(y);
            const float o = static_cast<float>(y); x[i] = std::abs(o) < 1e-30f ? 0.0f : o;
        }
    }
}

}  // namespace sw::lv13
