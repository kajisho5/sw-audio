#include "cr02/cr02.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace sw::cr02 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"cr02.grid",    "Grid",    0, 3, 1,    Curve::Step, 1, {0, 1, 2, 3}, "", {"1/8", "1/16", "1/32", "Triplet"}},
            {"cr02.gate",    "Gate",    0, 100, 60, Curve::Lin, 1, {}, "%"},
            {"cr02.repeat",  "Repeat",  1, 16, 4,   Curve::Step, 1, {1, 2, 4, 8, 16}, "", {"1x", "2x", "4x", "8x", "16x"}},
            {"cr02.pitch",   "Pitch",   -12, 12, 0, Curve::Lin, 1, {}, "st"},
            {"cr02.reverse", "Reverse", 0, 1, 0,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"cr02.filter",  "Filter",  200, 20000, 4000, Curve::Log, 1, {}, "Hz"},
            {"cr02.mix",     "Mix",     0, 100, 100, Curve::Lin, 1, {}, "%"},
        };
        static const bool kDefault[16] = {1, 0, 1, 0, 1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 0};
        static std::vector<std::string> names; names.reserve(32);   // ParamSpec keeps the pointers: the strings must stay where they are
        for (int i = 0; i < 16; ++i) { char id[16], nm[16]; std::snprintf(id, sizeof id, "cr02.step%02d", i + 1); std::snprintf(nm, sizeof nm, "Step %d", i + 1); names.push_back(id); names.push_back(nm); v.push_back({names[names.size() - 2].c_str(), names.back().c_str(), 0, 1, kDefault[i] ? 1.0 : 0.0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}, nullptr, nullptr, 1.0, false}); }
        return v;
    }();
    return s;
}

double gridBeats(int g) { static const double b[4] = {0.5, 0.25, 0.125, 1.0 / 3.0}; return b[std::clamp(g, 0, 3)]; }

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kRingBits = 17;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; onsetBar_.fill(-100); }

void Processor::setTransport(bool playing, double toBar) {
    if (!playing || toBar < 0.0) return;
    const double nb = std::fmod(4.0 - toBar + 4.0, 4.0);
    if (nb < barBeat_ - 2.0) ++bars_;
    barBeat_ = nb;
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    const size_t sz = static_cast<size_t>(1) << kRingBits; mask_ = sz - 1;
    for (auto& r : ring_) r.assign(sz, 0.0f);
    for (auto& s : slice_) s.assign(sz / 2, 0.0f);
    for (auto& f : lp_) f.reset();
    wpos_ = 0; barBeat_ = 0.0; bars_ = 0; lastStep_ = -1; active_ = false; env_ = slow_ = 0.0; onsetBar_.fill(-100); sinceStart_ = 0;
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::clearPattern() { for (int i = 0; i < 16; ++i) target_[static_cast<size_t>(Step01 + i)] = 0.0; }

void Processor::randomize() {
    const double sb = gridBeats(static_cast<int>(target_[Grid] + 0.5));
    auto next = [&]() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return (rng_ >> 8) * (1.0 / 16777216.0); };
    for (int s = 0; s < 16; ++s) {
        const double frac = std::fmod(s * sb, 1.0), eps = 1e-6;
        double p = frac < eps || frac > 1.0 - eps ? 0.85 : (std::abs(frac - 0.5) < eps ? 0.6 : ((std::abs(frac - 0.25) < eps || std::abs(frac - 0.75) < eps) ? 0.35 : 0.4));
        if (bars_ - onsetBar_[static_cast<size_t>(s)] <= 1) p = std::min(0.95, p + 0.3);
        target_[static_cast<size_t>(Step01 + s)] = next() < p ? 1.0 : 0.0;
    }
}

void Processor::startStep(int64_t idx) {
    const int slot = static_cast<int>(((idx % 16) + 16) % 16);
    if (!stepOn(slot)) { active_ = false; return; }
    const int rep = std::clamp(static_cast<int>(target_[Repeat] + 0.5), 1, 16);
    const double bpm = bpm_ > 0.0 ? bpm_ : 120.0;
    stepSamples_ = std::max(64, static_cast<int>(std::lround(gridBeats(static_cast<int>(target_[Grid] + 0.5)) * 60.0 / bpm * fs_)));
    sliceLen_ = std::clamp(stepSamples_ / rep, 32, static_cast<int>(slice_[0].size()));
    for (int c = 0; c < 2; ++c) for (int k = 0; k < sliceLen_; ++k) slice_[static_cast<size_t>(c)][static_cast<size_t>(k)] = ring_[static_cast<size_t>(c)][(wpos_ + (mask_ + 1) - static_cast<size_t>(sliceLen_) + static_cast<size_t>(k)) & mask_];
    sinceStart_ = 0; active_ = true; rep_ = rep;
    lp_[0].setup(Svf::Mode::LowPass, std::min(target_[Filter], 0.45 * fs_), fs_, 0.7071, 0.0); lp_[1].setup(Svf::Mode::LowPass, std::min(target_[Filter], 0.45 * fs_), fs_, 0.7071, 0.0);
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const double bpm = bpm_ > 0.0 ? bpm_ : 120.0, dBeat = bpm / 60.0 / fs_, sb = gridBeats(static_cast<int>(target_[Grid] + 0.5));
    const double mix = target_[Mix] * 0.01, gate = target_[Gate] * 0.01, rate = std::exp2(std::round(target_[Pitch]) / 12.0);
    const bool rev = target_[Reverse] > 0.5, filterOn = target_[Filter] < 19999.0;   // 20 kHz = the filter is out
    const double ea = 1.0 - std::exp(-1.0 / (0.005 * fs_)), es = 1.0 - std::exp(-1.0 / (0.1 * fs_));
    for (int i = 0; i < n; ++i) {
        double dry[2] = {0, 0};
        for (int c = 0; c < nch; ++c) dry[c] = ch[c][i];
        // onsets (for Randomize)
        const double lvl = std::abs(dry[0]) + (nch > 1 ? std::abs(dry[1]) : 0.0);
        env_ += ea * (lvl - env_); slow_ += es * (lvl - slow_);
        // position
        barBeat_ += dBeat; if (barBeat_ >= 4.0) { barBeat_ -= 4.0; ++bars_; }
        const int64_t idx = static_cast<int64_t>(std::floor((static_cast<double>(bars_) * 4.0 + barBeat_) / sb));
        if (env_ > 2.8 * slow_ && env_ > 2e-3 && idx == lastStep_) onsetBar_[static_cast<size_t>(((idx % 16) + 16) % 16)] = bars_;
        if (idx != lastStep_) { lastStep_ = idx; startStep(idx); }
        double out[2] = {dry[0], dry[1]};
        if (active_) {
            const int phase = sinceStart_ % sliceLen_;
            if (phase == 0) readPos_ = rev ? sliceLen_ - 1.0 : 0.0;
            const double open = gate * sliceLen_, fade = std::min(0.001 * fs_, std::max(1.0, open * 0.25));
            double g = 0.0;
            if (phase < open) g = std::min({1.0, (phase + 1) / fade, (open - phase) / fade});
            for (int c = 0; c < nch; ++c) {
                const auto& sl = slice_[static_cast<size_t>(c)];
                const int i0 = static_cast<int>(std::floor(readPos_)); const double fr = readPos_ - i0;
                const int a = std::clamp(i0, 0, sliceLen_ - 1), b = std::clamp(i0 + 1, 0, sliceLen_ - 1);
                double w = (sl[static_cast<size_t>(a)] * (1.0 - fr) + sl[static_cast<size_t>(b)] * fr) * g;
                if (filterOn) w = lp_[static_cast<size_t>(c)].process(w);
                out[c] = dry[c] + mix * (w - dry[c]);
            }
            readPos_ += rev ? -rate : rate;
            if (readPos_ >= sliceLen_) readPos_ -= sliceLen_; if (readPos_ < 0.0) readPos_ += sliceLen_;
            ++sinceStart_;
            if (sinceStart_ >= stepSamples_) active_ = false;
        }
        for (int c = 0; c < nch; ++c) { double y = out[c]; if (!std::isfinite(y) || std::abs(y) < 1e-30) y = 0.0; ch[c][i] = static_cast<float>(y); }
        for (int c = 0; c < nch; ++c) ring_[static_cast<size_t>(c)][wpos_] = static_cast<float>(dry[c]);   // after the step logic: the slice ends with the sample before this one
        if (nch == 1) ring_[1][wpos_] = static_cast<float>(dry[0]);
        wpos_ = (wpos_ + 1) & mask_;
    }
    (void)kPi;
}

}  // namespace sw::cr02
