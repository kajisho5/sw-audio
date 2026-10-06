#include "md04/md04.hpp"
#include "sw/notes.hpp"
#include <algorithm>
#include <cmath>

namespace sw::md04 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"md04.mode",  "Mode",  0, 2, 1,      Curve::Step, 1, {0, 1, 2}, "", {"Tremolo", "Auto pan", "Harmonic"}},
        {"md04.rate",  "Rate",  0.1, 20, 4,   Curve::Log, 1, {}, "Hz"},
        {"md04.depth", "Depth", 0, 100, 60,   Curve::Lin, 1, {}, "%"},
        {"md04.shape", "Shape", 0, 3, 0,      Curve::Step, 1, {0, 1, 2, 3}, "", {"Sine", "Triangle", "Square", "Ramp"}},
        {"md04.width", "Width", 0, 100, 100,  Curve::Lin, 1, {}, "%"},
        {"md04.sync",  "Sync",  0, 1, 1,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846, kSplitHz = 800.0, kEdgeSeconds = 0.002;
}

double lfoValue(int shape, double p) {
    p -= std::floor(p);
    switch (shape) {
        case Triangle: return p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p;
        case Square: return p < 0.5 ? 1.0 : -1.0;
        case Ramp: return 2.0 * p - 1.0;
        default: return std::sin(2.0 * kPi * p);
    }
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
    for (auto& s : split_) {
        s.lp1.setup(Svf::Mode::LowPass, kSplitHz, fs_, 0.7071, 0.0); s.lp2.setup(Svf::Mode::LowPass, kSplitHz, fs_, 0.7071, 0.0);
        s.hp1.setup(Svf::Mode::HighPass, kSplitHz, fs_, 0.7071, 0.0); s.hp2.setup(Svf::Mode::HighPass, kSplitHz, fs_, 0.7071, 0.0);
        s.lp1.reset(); s.lp2.reset(); s.hp1.reset(); s.hp2.reset();
    }
    ph_ = 0.0; slew_[0] = slew_[1] = lfoValue(static_cast<int>(target_[Shape] + 0.5), 0.0);
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::snapToTargets() {}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const double hz = rateHz(), inc = hz / fs_;
    if (syncPending_) {
        const double period = fs_ / hz, toBar = syncBeats_ * 60.0 / bpm_ * fs_;
        ph_ = 1.0 - std::fmod(toBar, period) / period; if (ph_ >= 1.0) ph_ -= 1.0;
        syncPending_ = false;
    }
    const int mode = static_cast<int>(target_[Mode] + 0.5), shape = static_cast<int>(target_[Shape] + 0.5);
    const double depth = target_[Depth] * 0.01, width = target_[Width] * 0.01;
    const bool edges = shape == Square || shape == Ramp;
    const double slewK = 1.0 - std::exp(-1.0 / (kEdgeSeconds * fs_));
    const double rOff = width * 0.5;
    for (int i = 0; i < n; ++i) {
        ph_ += inc; if (ph_ >= 1.0) ph_ -= 1.0;
        if (mode == AutoPan) {
            // one LFO for both channels; the position is balanced between them
            const double raw = lfoValue(shape, ph_);
            slew_[0] = edges ? slew_[0] + slewK * (raw - slew_[0]) : raw;
            lastLfo_[0] = lastLfo_[1] = slew_[0];
            const double theta = kPi / 4.0 * (1.0 + std::clamp(depth * width * slew_[0], -1.0, 1.0));
            const double gl = std::sqrt(2.0) * std::cos(theta), gr = std::sqrt(2.0) * std::sin(theta);
            if (nch > 1) {
                double l = ch[0][i] * gl, r = ch[1][i] * gr;
                if (std::abs(l) < 1e-30) l = 0.0;
                if (std::abs(r) < 1e-30) r = 0.0;
                ch[0][i] = static_cast<float>(l); ch[1][i] = static_cast<float>(r);
            } else ch[0][i] = static_cast<float>(ch[0][i] * (gl + gr) * 0.5);
            continue;
        }
        for (int c = 0; c < nch; ++c) {
            double p = ph_ + (c == 0 ? 0.0 : rOff); p -= std::floor(p);
            const double raw = lfoValue(shape, p);
            slew_[c] = edges ? slew_[c] + slewK * (raw - slew_[c]) : raw;
            lastLfo_[static_cast<size_t>(c)] = slew_[c];
            const double uni = 0.5 * (slew_[c] + 1.0);
            const double x = ch[c][i];
            double y;
            if (mode == Tremolo) y = x * (1.0 - depth * (1.0 - uni));
            else {
                auto& s = split_[static_cast<size_t>(c)];
                const double lo = s.lp2.process(s.lp1.process(x)), hi = s.hp2.process(s.hp1.process(x));
                y = lo * (1.0 - depth * (1.0 - uni)) + hi * (1.0 - depth * uni);
            }
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::md04
