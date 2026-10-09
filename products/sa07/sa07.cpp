#include "sa07/sa07.hpp"
#include <algorithm>
#include <cmath>

namespace sw::sa07 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"sa07.era",       "Era",       0, 3, 1,         Curve::Step, 1, {0, 1, 2, 3}, "", {"1950", "1970", "1990", "Tape"}},
            {"sa07.crackle",   "Crackle",   0, 10, 0,        Curve::Lin,  1, {}, ""},
            {"sa07.dust",      "Dust",      0, 10, 0,        Curve::Lin,  1, {}, ""},
            {"sa07.wow",       "Wow",       0, 10, 0,        Curve::Lin,  1, {}, ""},
            {"sa07.bandwidth", "Bandwidth", 3000, 20000, 20000, Curve::Log, 1, {}, "Hz"},
            {"sa07.mono",      "Mono",      0, 100, 0,       Curve::Lin,  1, {}, "%"},
            {"sa07.mix",       "Mix",       0, 100, 100,     Curve::Lin,  1, {}, "%"},
            unitSpec("sa07.unit"),
        };
        v[Bandwidth].minLabel = "Narrow"; v[Bandwidth].maxLabel = "Full"; v[Mono].minLabel = "Off";
        return v;
    }();
    return s;
}

namespace {
struct EraPreset { double crackle, wow, bandwidth, lowCutHz; };   // design values
constexpr EraPreset kEra[4] = {{6.0, 4.0, 4500.0, 150.0}, {3.0, 2.0, 10000.0, 60.0}, {0.0, 0.5, 16000.0, 30.0}, {0.0, 3.0, 12000.0, 40.0}};
constexpr double kPopRate = 30.0, kDustRate = 600.0, kPopPeakDb = -24.0, kDustPeakDb = -50.0, kPopHz = 4000.0;
double u01(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s / 4294967296.0; }
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; writes_.reserve(8); }

int Processor::latencySamples() const { return std::max(2, static_cast<int>(std::lround(0.001 * fs_))); }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    wob_.prepare(fs_, latencySamples());
    c_[0].rng = 0x1badf00du; c_[1].rng = 0x2545f491u;
    for (auto& c : c_) c.pop.setup(Svf::Mode::BandPass, kPopHz, fs_, 2.0, 0);
    writes_.clear(); begun_ = false;
    updateFilters();
}

void Processor::updateFilters() {
    const int era = static_cast<int>(target_[Era] + 0.5);
    for (auto& c : c_) {
        c.hp.setup(Svf::Mode::HighPass, kEra[era].lowCutHz, fs_, 0.70710678, 0);
        c.lp.setup(Svf::Mode::LowPass, std::min(target_[Bandwidth], fs_ * 0.45), fs_);
    }
}

void Processor::applyParam(int id, double v) { target_[static_cast<size_t>(id)] = v; if (id == Era || id == Bandwidth) updateFilters(); }

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    applyParam(id, v);
    // an explicit value set after the Era (the same event batch, or later by hand) cancels that parameter's pending preset write
    writes_.erase(std::remove_if(writes_.begin(), writes_.end(), [id](const std::pair<int, double>& w) { return w.first == id && id != Era; }), writes_.end());
    if (id == Era) {   // the era moves Crackle, Wow and Bandwidth in one go (the user can change any of them afterwards)
        const EraPreset& p = kEra[static_cast<int>(v + 0.5)];
        applyParam(Crackle, p.crackle); applyParam(Wow, p.wow); applyParam(Bandwidth, p.bandwidth);
        writes_.clear(); writes_.push_back({Crackle, p.crackle}); writes_.push_back({Wow, p.wow}); writes_.push_back({Bandwidth, p.bandwidth});
    }
}

int Processor::takeParamWrite(int& id, double& plain) {
    if (writes_.empty()) return 0;
    id = writes_.front().first; plain = writes_.front().second; writes_.erase(writes_.begin());
    return 7;   // each parameter as its own gesture: begin + value + end
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const double wowS = target_[Wow] / 10.0 * 0.4e-3 * fs_, flS = target_[Wow] / 10.0 * 0.016e-3 * fs_ * (target_[Era] > 2.5 ? 2.0 : 1.0);
    const double popP = target_[Crackle] / 10.0 * kPopRate / fs_, dustP = target_[Dust] / 10.0 * kDustRate / fs_;
    const double popAmp = std::pow(10.0, (kPopPeakDb - 20.0 * (1.0 - target_[Crackle] / 10.0)) / 20.0);   // 10 -> -24 dBFS, 3 -> -38 dBFS
    const double dustAmp = std::pow(10.0, (kDustPeakDb - 16.0 * (1.0 - target_[Dust] / 10.0)) / 20.0);
    const bool bwFull = target_[Bandwidth] >= 19999.0, mono = target_[Mono] > 0.0;
    const double side = 1.0 - target_[Mono] / 100.0;
    for (int i = 0; i < n; ++i) {
        wob_.advance(wowS, flS);
        double y[2] = {0, 0};
        for (int c = 0; c < nch; ++c) { Ch& s = c_[static_cast<size_t>(c)]; double v = wob_.read(c, ch[c][i]); v = s.hp.process(v); if (!bwFull) v = s.lp.process(v); y[c] = v; }
        wob_.step();
        if (mono && nch > 1) { const double m = 0.5 * (y[0] + y[1]), d = 0.5 * (y[0] - y[1]) * side; y[0] = m + d; y[1] = m - d; }
        for (int c = 0; c < nch; ++c) {
            Ch& s = c_[static_cast<size_t>(c)];
            double add = 0;
            if (popP > 0.0) { double e = 0; if (u01(s.rng) < popP) { const double r = u01(s.rng); e = (u01(s.rng) < 0.5 ? -1.0 : 1.0) * popAmp * (0.3 + 0.7 * r * r) * 3.0; } add += s.pop.process(e); }
            if (dustP > 0.0 && u01(s.rng) < dustP) add += (u01(s.rng) < 0.5 ? -1.0 : 1.0) * dustAmp * u01(s.rng);
            double out = y[c] + add;
            if (!std::isfinite(out)) out = 0.0;
            if (std::abs(out) < 1e-30) out = 0.0;
            ch[c][i] = static_cast<float>(out);
        }
    }
}

}  // namespace sw::sa07
