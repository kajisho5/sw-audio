#include "lv12/lv12.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace sw::lv12 {
namespace {
constexpr double kCenters[kBands] = {20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000};
constexpr double kQ = 4.3;
std::string label(int b) { const double f = kCenters[b]; char t[24]; if (f >= 1000) std::snprintf(t, sizeof t, "%g kHz", f / 1000.0); else std::snprintf(t, sizeof t, "%g Hz", f); return t; }
}

double bandCenterHz(int b) { return kCenters[std::clamp(b, 0, kBands - 1)]; }

const std::vector<ParamSpec>& specs() {
    static std::vector<std::string> names, ids;
    static const std::vector<ParamSpec> s = [] {
        names.reserve(2 * kBands); ids.reserve(2 * kBands);
        std::vector<ParamSpec> v;
        for (int b = 0; b < kBands; ++b) { names.push_back("Band " + label(b)); ids.push_back("lv12.band." + std::to_string(b + 1)); v.push_back({ids.back().c_str(), names.back().c_str(), -12, 12, 0, Curve::Lin, 1, {}, "dB"}); }
        v.push_back({"lv12.edit", "Edit", 0, 2, 2, Curve::Step, 1, {0, 1, 2}, "", {"Left", "Right", "Both"}});
        v.push_back({"lv12.link", "Link L/R", 0, 1, 1, Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
        v.push_back({"lv12.hpf", "HPF", 20, 200, 40, Curve::Log, 1, {}, "Hz"});
        v.push_back({"lv12.lpf", "LPF", 5000, 20000, 18000, Curve::Log, 1, {}, "Hz"});
        v.push_back({"lv12.output", "Output", -12, 12, 0, Curve::Lin, 1, {}, "dB"});
        v.push_back({"lv12.rta", "RTA overlay", 0, 1, 1, Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
        v.push_back({"lv12.fbguard", "Feedback guard", 0, 1, 1, Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
        for (int b = 0; b < kBands; ++b) { names.push_back("R " + label(b)); ids.push_back("lv12.bandr." + std::to_string(b + 1)); v.push_back({ids.back().c_str(), names.back().c_str(), -12, 12, 0, Curve::Lin, 1, {}, "dB"}); }
        v[Hpf].minLabel = "Off"; v[Lpf].maxLabel = "Off"; v[Lpf].maxLabelNorm = 0.98;
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int maxBlock) {
    fs_ = sampleRate; guard_.prepare(fs_, 12); guard_.setParams(2, -12, 0.1, 6.0); guard_.setApply(false); rta_.setup(fs_, 4096, 1.0);
    for (auto& c : c_) { for (auto& f : c.band) f.reset(); c.hp.reset(); c.lp.reset(); c.on.fill(false); }
    mono_.assign(static_cast<size_t>(std::max(1, maxBlock) + 8), 0.0f); fb_.assign(static_cast<size_t>(std::max(1, maxBlock) + 8) * 2, 0.0f);
    prepared_ = true; writes_.clear(); updateAll(0);
}

void Processor::updateBand(int c, int b, int ramp) {
    const bool right = c == 1 && target_[LinkLR] < 0.5;
    const double g = target_[static_cast<size_t>(right ? BandR0 + b : Band0 + b)];
    Chan& ch = c_[static_cast<size_t>(c)]; const size_t k = static_cast<size_t>(b);
    const bool on = std::abs(g) > 0.005 && kCenters[b] < fs_ * 0.45;
    if (on) { if (!ch.on[k] || ramp <= 1) ch.band[k].setup(Svf::Mode::Bell, kCenters[b], fs_, kQ, g); else ch.band[k].setupRamp(Svf::Mode::Bell, kCenters[b], fs_, kQ, g, ramp); }
    else if (ch.on[k] && ramp > 1) ch.band[k].setupRamp(Svf::Mode::Bell, kCenters[b], fs_, kQ, 0.0, ramp);
    ch.on[k] = on || (ch.on[k] && ramp > 1);   // a band going to 0 stays in the chain until the next full update
}
void Processor::updateAll(int ramp) {
    for (int c = 0; c < 2; ++c) {
        for (int b = 0; b < kBands; ++b) updateBand(c, b, ramp);
        c_[static_cast<size_t>(c)].hp.setup(Svf::Mode::HighPass, std::max(20.0, target_[Hpf]), fs_, 0.70710678, 0);
        c_[static_cast<size_t>(c)].lp.setup(Svf::Mode::LowPass, std::min(target_[Lpf], fs_ * 0.45), fs_, 0.70710678, 0);
    }
    hpOn_ = target_[Hpf] > 20.0 * 1.0001; lpOn_ = target_[Lpf] < 20000.0 * 0.9999;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id != Edit) writes_.erase(std::remove_if(writes_.begin(), writes_.end(), [id](const std::pair<int, double>& w) { return w.first == id; }), writes_.end());
    if (!prepared_) return;
    if (id >= Band0 && id < Band0 + kBands) { updateBand(0, id - Band0, 256); if (target_[LinkLR] > 0.5) updateBand(1, id - Band0, 256); }
    else if (id >= BandR0 && id < BandR0 + kBands) { if (target_[LinkLR] < 0.5) updateBand(1, id - BandR0, 256); }
    else if (id == LinkLR) for (int b = 0; b < kBands; ++b) updateBand(1, b, 256);
    else if (id == Hpf || id == Lpf) updateAll(0);
}

void Processor::flat() {
    writes_.clear();
    for (int b = 0; b < kBands; ++b) { target_[static_cast<size_t>(Band0 + b)] = 0; target_[static_cast<size_t>(BandR0 + b)] = 0; writes_.push_back({Band0 + b, 0.0}); writes_.push_back({BandR0 + b, 0.0}); }
    if (prepared_) for (int c = 0; c < 2; ++c) for (int b = 0; b < kBands; ++b) updateBand(c, b, 256);
}
int Processor::takeParamWrite(int& id, double& plain) {
    if (writes_.empty()) return 0;
    id = writes_.front().first; plain = writes_.front().second; writes_.erase(writes_.begin());
    return 7;
}

unsigned Processor::flaggedBands() const {
    unsigned m = 0;
    for (int i = 0; i < guard_.slots(); ++i) { const auto& s = guard_.slot(i); if (!s.used || s.freeing || s.targetDb <= 0) continue;
        int best = 0; double bd = 1e9; for (int b = 0; b < kBands; ++b) { const double d = std::abs(std::log2(s.freq / kCenters[b])); if (d < bd) { bd = d; best = b; } }
        m |= 1u << best; }
    return m;
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    const int nc = std::min(numCh, 2);
    if (target_[RtaOverlay] > 0.5) { std::vector<float>& m = mono_; if (m.size() < static_cast<size_t>(n)) m.assign(static_cast<size_t>(n), 0.0f); for (int i = 0; i < n; ++i) m[static_cast<size_t>(i)] = nc > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i]; rta_.process(m.data(), n); }
    if (target_[FeedbackGuardOn] > 0.5) { std::vector<float>& a = fb_; if (a.size() < static_cast<size_t>(n) * 2) a.assign(static_cast<size_t>(n) * 2, 0.0f);
        float* cp[2] = {a.data(), a.data() + n}; for (int c = 0; c < nc; ++c) std::copy(ch[c], ch[c] + n, cp[c]); guard_.process(cp, nc, n); }
    for (int c = 0; c < nc; ++c) {
        Chan& s = c_[static_cast<size_t>(c)]; float* x = ch[c];
        for (int i = 0; i < n; ++i) {
            double y = x[i];
            if (hpOn_) y = s.hp.process(y);
            for (int b = 0; b < kBands; ++b) if (s.on[static_cast<size_t>(b)]) y = s.band[static_cast<size_t>(b)].process(y);
            if (lpOn_) y = s.lp.process(y);
            const float o = static_cast<float>(y); x[i] = std::abs(o) < 1e-30f ? 0.0f : o;
        }
    }
}

}  // namespace sw::lv12
