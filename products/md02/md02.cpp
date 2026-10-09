#include "md02/md02.hpp"
#include "sw/tail.hpp"
#include "sw/notes.hpp"
#include <algorithm>
#include <cmath>

namespace sw::md02 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"md02.rate",     "Rate",     0.01, 10, 0.2, Curve::Log, 1, {}, "Hz"},
        {"md02.depth",    "Depth",    0, 100, 70,    Curve::Lin, 1, {}, "%"},
        {"md02.feedback", "Feedback", -100, 100, 60, Curve::Lin, 1, {}, "%"},
        {"md02.manual",   "Manual",   0.1, 10, 3,    Curve::Log, 1, {}, "ms"},
        {"md02.evo.on",   "Through zero", 0, 1, 1,   Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"md02.sync",     "Sync",     0, 1, 0,       Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"md02.mix",      "Mix",      0, 100, 50,    Curve::Lin, 1, {}, "%"},
    };
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846, kTzSeconds = 0.010, kLimit = 4.0;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

int Processor::latencySamples() const { return target_[ThroughZero] > 0.5 ? static_cast<int>(std::lround(kTzSeconds * fs_)) : 0; }

double Processor::rateHz() const {
    double hz = target_[Rate];
    if (target_[Sync] > 0.5 && bpm_ > 0.0) hz = 1.0 / noteSeconds(noteNearest(1.0 / hz, 120.0), bpm_);
    return hz;
}

void Processor::setTransport(bool playing, double beatsToNextBar) { syncPending_ = playing && target_[Sync] > 0.5 && beatsToNextBar >= 0.0 && bpm_ > 0.0; syncBeats_ = beatsToNextBar; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    size_t sz = 16; while (sz < static_cast<size_t>(0.1 * fs_) + 16) sz <<= 1;
    mask_ = sz - 1;
    for (auto& b : buf_) b.assign(sz, 0.0f);
    pos_ = 0; ph_ = 0.0; fbState_[0] = fbState_[1] = 0.0;
    latency_ = latencySamples();
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::snapToTargets() {}

double Processor::read(int c, double d) const {
    const auto& b = buf_[static_cast<size_t>(c)];
    const double rp = static_cast<double>(pos_) - d;
    const double fl = std::floor(rp), f = rp - fl;
    const long i = static_cast<long>(fl);
    auto at = [&](long k) { return static_cast<double>(b[static_cast<size_t>(k) & mask_]); };
    const double y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const double c0 = y1, c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + c0;
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const double hz = rateHz(), inc = hz / fs_;
    if (syncPending_) {
        const double period = fs_ / hz, toBar = syncBeats_ * 60.0 / bpm_ * fs_;
        ph_ = 1.0 - std::fmod(toBar, period) / period; if (ph_ >= 1.0) ph_ -= 1.0;
        syncPending_ = false;
    }
    const bool tz = latency_ > 0;
    const double depth = target_[Depth] * 0.01, fb = target_[Feedback] * 0.01, manual = target_[Manual] * 0.001 * fs_;
    for (int i = 0; i < n; ++i) {
        ph_ += inc; if (ph_ >= 1.0) ph_ -= 1.0;
        double out[2];
        for (int c = 0; c < 2; ++c) {
            double p = ph_ - (c == 0 ? 0.0 : 0.25); p -= std::floor(p);
            const double s = std::sin(2.0 * kPi * p);
            double d = tz ? latency_ + manual * depth * s : manual * std::pow(2.0, 1.5 * depth * s);
            d = std::clamp(d, 3.0, static_cast<double>(mask_) - 8.0);
            lastDelay_[c] = d;
            out[c] = read(c, d);
        }
        for (int c = 0; c < 2; ++c) {
            const double in = c < nch ? ch[c][i] : ch[0][i];
            const double x = in + fb * kLimit * std::tanh(out[c] / kLimit);
            buf_[static_cast<size_t>(c)][pos_ & mask_] = static_cast<float>(x);
        }
        ++pos_;
        for (int c = 0; c < nch; ++c) { double y = out[c]; if (std::abs(y) < 1e-30) y = 0.0; ch[c][i] = static_cast<float>(y); }
    }
}

// the tail: the flanger feeds back through a delay of Manual plus the sweep (up to Manual x 2 + 2 ms); each trip is Feedback (either sign) of the last
double Processor::tailSeconds() const { return tail::loop(target_[Manual] * 0.002 + 0.002, std::abs(target_[Feedback]) * 0.01) + 0.1; }

}  // namespace sw::md02
