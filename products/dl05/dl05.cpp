#include "dl05/dl05.hpp"
#include "sw/notes.hpp"
#include <algorithm>
#include <cmath>

namespace sw::dl05 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v;
        v.push_back({"dl05.mode",   "Mode",       0, 2, 0,      Curve::Step, 1, {0, 1, 2}, "", {"Reverse", "Forward", "Random"}});
        std::vector<double> steps; std::vector<std::string> labels;
        for (int i = 0; i < kNumTimes; ++i) { steps.push_back(i); labels.push_back(noteName(5 + i)); }
        v.push_back({"dl05.time",   "Time",       0, kNumTimes - 1, 6, Curve::Step, 1, steps, "", labels});
        v.push_back({"dl05.grain",  "Grain size", 10, 500, 80,  Curve::Log, 1, {}, "ms"});
        v.push_back({"dl05.spray",  "Spray",      0, 100, 30,   Curve::Lin, 1, {}, "%"});
        v.push_back({"dl05.pitch",  "Pitch +12",  0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
        v.push_back({"dl05.freeze", "Freeze",     0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
        v.push_back({"dl05.mix",    "Mix",        0, 100, 40,   Curve::Lin, 1, {}, "%"});
        return v;
    }();
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

double Processor::timeSamples() const {
    const double bpm = bpm_ > 0.0 ? bpm_ : 120.0;
    const int idx = 5 + std::clamp(static_cast<int>(target_[Time] + 0.5), 0, kNumTimes - 1);
    return std::min(noteSeconds(idx, bpm), kMaxSeconds) * fs_;
}

void Processor::setTransport(bool playing, double beatsToNextBar) {
    syncPending_ = playing && beatsToNextBar >= 0.0 && bpm_ > 0.0;
    syncBeats_ = beatsToNextBar;
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    size_t sz = 16; while (sz < static_cast<size_t>(10.5 * fs_)) sz <<= 1;
    mask_ = sz - 1;
    for (auto& b : buf_) b.assign(sz, 0.0f);
    w_ = 0; validFrom_ = 0; u_ = 0.0; tf_ = 0.0; frozen_ = false; hopLeft_ = 0;
    for (auto& g : grain_) g.on = false;
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::snapToTargets() {}

double Processor::read(int c, double src) const {
    const double ref = frozen_ ? tf_ : static_cast<double>(w_);   // the newest recorded sample is just below it
    const double lo = std::max(static_cast<double>(validFrom_), ref - static_cast<double>(mask_) + 8.0);
    if (src < lo + 2.0 || src > ref - 3.0) return 0.0;
    const auto& b = buf_[static_cast<size_t>(c)];
    const double fl = std::floor(src), f = src - fl;
    const long i = static_cast<long>(fl);
    auto at = [&](long k) { return static_cast<double>(b[static_cast<size_t>(k) & mask_]); };
    const double y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const double c0 = y1, c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + c0;
}

void Processor::spawn() {
    Grain* g = nullptr;
    for (auto& x : grain_) if (!x.on) { g = &x; break; }
    if (!g) return;
    const double P = timeSamples(), now = static_cast<double>(w_);
    const int mode = static_cast<int>(target_[Mode] + 0.5);
    const double hop = std::max(2.0, std::round(target_[GrainSize] * 0.001 * fs_ * 0.5));
    g->len = 2.0 * hop; g->tau = 0.0; g->rate = target_[Pitch] > 0.5 ? 2.0 : 1.0;
    // the read pointer must move by rate x hop from grain to grain, or the overlapping grains disagree in phase and cancel (a pitch-shifted tone would vanish)
    if (mode == Reverse) { g->dir = -1.0; g->s0 = now - (1.0 + g->rate) * u_; }
    else if (mode == Forward) {
        g->dir = 1.0;
        const double reset = g->rate > 1.0 ? P / (2.0 * (g->rate - 1.0)) : P;   // the delay shrinks by (rate - 1) per sample: start again from P before it reaches the write head
        g->s0 = now - P + (g->rate - 1.0) * std::fmod(u_, reset);
    }
    else { g->dir = rnd() < 0.5 ? -1.0 : 1.0; g->s0 = now - rnd() * P; }
    g->s0 += (rnd() * 2.0 - 1.0) * target_[Spray] * 0.01 * g->len;
    g->on = true;
    hopLeft_ = static_cast<int>(hop);
}

void Processor::grains(double* out) const {
    for (int i = 0; i < kGrainSlots * kGrainValues; ++i) out[i] = 0.0;
    if (!prepared_) return;
    const double P = timeSamples(), ref = frozen_ ? tf_ : static_cast<double>(w_);
    for (int i = 0; i < kGrainSlots; ++i) {
        const Grain& g = grain_[static_cast<size_t>(i)];
        if (!g.on) continue;
        double src = g.s0 + g.dir * g.rate * g.tau;
        if (frozen_) { const double base = tf_ - P; src = base + std::fmod(std::fmod(src - base, P) + P, P); }   // as process() reads it
        double* o = out + i * kGrainValues;
        o[0] = 1.0; o[1] = std::max(0.0, (ref - src) / fs_); o[2] = g.dir * g.rate; o[3] = g.tau / g.len; o[4] = g.len * g.rate / fs_;
    }
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const double P = timeSamples();
    if (syncPending_) {
        const double toBar = syncBeats_ * 60.0 / bpm_ * fs_;
        u_ = P - std::fmod(toBar, P); if (u_ >= P) u_ -= P;
        syncPending_ = false;
    }
    const bool wantFreeze = target_[Freeze] > 0.5;
    for (int i = 0; i < n; ++i) {
        if (wantFreeze != frozen_) {
            frozen_ = wantFreeze;
            if (frozen_) tf_ = static_cast<double>(w_); else validFrom_ = w_;
        }
        if (u_ >= P) u_ = std::fmod(u_, P);
        if (--hopLeft_ <= 0) spawn();
        double out[2] = {0.0, 0.0};
        for (auto& g : grain_) {
            if (!g.on) continue;
            double src = g.s0 + g.dir * g.rate * g.tau;
            if (frozen_) { const double base = tf_ - P; src = base + std::fmod(std::fmod(src - base, P) + P, P); }
            const double win = 0.5 * (1.0 - std::cos(2.0 * kPi * g.tau / g.len));
            for (int c = 0; c < 2; ++c) out[c] += win * read(c, src);
            if (++g.tau >= g.len) g.on = false;
        }
        const size_t at = static_cast<size_t>(w_) & mask_;
        if (!frozen_) for (int c = 0; c < 2; ++c) buf_[static_cast<size_t>(c)][at] = c < nch ? ch[c][i] : ch[0][i];
        ++w_; u_ += 1.0;
        for (int c = 0; c < nch; ++c) { double y = c == 0 ? out[0] : out[1]; if (nch == 1) y = out[0]; if (std::abs(y) < 1e-30) y = 0.0; ch[c][i] = static_cast<float>(y); }
    }
}

}  // namespace sw::dl05
