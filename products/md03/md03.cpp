#include "md03/md03.hpp"
#include "sw/notes.hpp"
#include <algorithm>
#include <cmath>

namespace sw::md03 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"md03.stages",   "Stages",   4, 12, 6,      Curve::Step, 1, {4, 6, 8, 12}, "", {"4", "6", "8", "12"}},
        {"md03.rate",     "Rate",     0.05, 8, 0.5,  Curve::Log, 1, {}, "Hz"},
        {"md03.depth",    "Depth",    0, 10, 5,      Curve::Lin, 1, {}, ""},
        {"md03.feedback", "Feedback", 0, 10, 3,      Curve::Lin, 1, {}, ""},
        {"md03.center",   "Center",   200, 4000, 800, Curve::Log, 1, {}, "Hz"},
        {"md03.mix",      "Mix",      0, 100, 50,    Curve::Lin, 1, {}, "%"},
        {"md03.sync",     "Sync",     0, 1, 0,       Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"md03.evo.on",   "Note follow", 0, 1, 0,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            unitSpec("md03.unit"),
    };
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

double Processor::rateHz() const {
    double hz = target_[Rate];
    if (target_[Sync] > 0.5 && bpm_ > 0.0) hz = 1.0 / noteSeconds(noteNearest(1.0 / hz, 120.0), bpm_);
    return hz;
}

void Processor::setTransport(bool playing, double beatsToNextBar) { syncPending_ = playing && target_[Sync] > 0.5 && beatsToNextBar >= 0.0 && bpm_ > 0.0; syncBeats_ = beatsToNextBar; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& z : z_) z.fill(0.0);
    fbState_[0] = fbState_[1] = 0.0; ph_ = 0.0; f0Smooth_ = 0.0; centreEff_ = target_[Center];
    pitch_.prepare(fs_);
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::snapToTargets() { if (prepared_) centreEff_ = target_[Center]; }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const double hz = rateHz(), inc = hz / fs_;
    if (syncPending_) {
        const double period = fs_ / hz, toBar = syncBeats_ * 60.0 / bpm_ * fs_;
        ph_ = 1.0 - std::fmod(toBar, period) / period; if (ph_ >= 1.0) ph_ -= 1.0;
        syncPending_ = false;
    }
    const int stages = std::clamp(static_cast<int>(target_[Stages] + 0.5), 1, 12);
    const double depthOct = target_[Depth] * 0.2, fb = target_[Feedback] * 0.09;
    const bool follow = target_[NoteFollow] > 0.5;
    const double glide = 1.0 - std::exp(-1.0 / (0.15 * fs_));
    for (int i = 0; i < n; ++i) {
        ph_ += inc; if (ph_ >= 1.0) ph_ -= 1.0;
        if (follow) {
            pitch_.push(nch > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i]);
            if (pitch_.voiced() && pitch_.f0() > 0.0) { if (f0Smooth_ <= 0.0) f0Smooth_ = pitch_.f0(); else f0Smooth_ += glide * (pitch_.f0() - f0Smooth_); }
        }
        const double base = target_[Center] * (follow && f0Smooth_ > 0.0 ? std::clamp(f0Smooth_ / kRefHz, 0.25, 8.0) : 1.0);
        centreEff_ = base;
        double out[2];
        for (int c = 0; c < nch; ++c) {
            double p = ph_ - (c == 0 ? 0.0 : 0.25); p -= std::floor(p);
            const double fc = std::clamp(base * std::pow(2.0, depthOct * std::sin(2.0 * kPi * p)), 20.0, 0.45 * fs_);
            lastFc_[static_cast<size_t>(c)] = fc;
            const double t = std::tan(kPi * fc / fs_), a = (t - 1.0) / (t + 1.0);
            double x = ch[c][i] + fb * fbState_[static_cast<size_t>(c)];
            auto& z = z_[static_cast<size_t>(c)];
            for (int k = 0; k < stages; ++k) { const double y = a * x + z[static_cast<size_t>(k)]; z[static_cast<size_t>(k)] = x - a * y; x = y; }
            fbState_[static_cast<size_t>(c)] = x;
            out[c] = x;
        }
        for (int c = 0; c < nch; ++c) { double y = out[c]; if (std::abs(y) < 1e-30) y = 0.0; ch[c][i] = static_cast<float>(y); }
    }
}

}  // namespace sw::md03
