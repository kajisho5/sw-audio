#include "dy02/dy02.hpp"
#include <algorithm>
#include <cmath>

namespace sw::dy02 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"dy02.level",      "Level",    0, 10, 0,     Curve::Lin,  1, {}, ""},
        {"dy02.out",        "Output",   -12, 24, 0,   Curve::Lin,  1, {}, "dB"},
        {"dy02.speed",      "Speed",    0, 2, 1,      Curve::Step, 1, {0, 1, 2}, "", {"Fast", "Prog", "Slow"}},
        {"dy02.target",     "Target",   -30, -6, -18, Curve::Lin,  1, {}, "LUFS"},
        {"dy02.emph",       "Emphasis", 0, 12, 0,     Curve::Lin,  1, {}, "dB"},
        {"dy02.mix",        "Mix",      0, 100, 100,  Curve::Lin,  1, {}, "%"},
        {"dy02.evo.on",     "Ride",     0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"dy02.automakeup", "Auto makeup", 0, 1, 0,   Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            unitSpec("dy02.unit"),
    };
    return s;
}

namespace {
constexpr double kRatio = 3.0, kKneeDb = 12.0;          // design values
constexpr double kRideMaxDb = 12.0, kRideFollowS = 1.5; // spec: +-12 dB, follow 1..2 s
constexpr double kRideGateLufs = -50.0;                 // spec: no action below -50 (breaths, silence)
constexpr int kChunk = 64;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& d : det_) d.set(fs_, LevelDetector::Mode::Rms);
    attackC_ = Ballistics::coef(fs_, 10.0);
    memAtt_ = Ballistics::coef(fs_, 2000.0);
    memRel_ = Ballistics::coef(fs_, 6000.0);
    avgC_ = Ballistics::coef(fs_, 2000.0);
    rideC_ = std::exp(-static_cast<double>(kChunk) / (kRideFollowS * fs_));
    meter_.setup(fs_, 2);
    gr_ = grFast_ = grSlow_ = memory_ = avgGr_ = rideDb_ = makeupDb_ = 0;
    scratch_.assign(static_cast<size_t>(std::max(2, kChunk)), 0.0f);
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case Level: updateCurve(); break;
        case Speed: {
            const int s = static_cast<int>(v + 0.5);
            if (s == 0) { fastC_ = Ballistics::coef(fs_, 40.0); slowC_ = Ballistics::coef(fs_, 500.0); }
            else if (s == 2) { fastC_ = Ballistics::coef(fs_, 200.0); slowC_ = Ballistics::coef(fs_, 3000.0); }
            else fastC_ = Ballistics::coef(fs_, 60.0);   // Prog: the slow stage is set per sample from the memory
            break;
        }
        case Emphasis:
            emphOn_ = v > 0.05;
            for (auto& f : emph_) f.setup(Svf::Mode::HighShelf, 2000.0, fs_, 0.70710678, v);
            break;
        default: break;
    }
}

void Processor::updateCurve() { comp_.set(-4.0 * target_[Level], kRatio, kKneeDb); }

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const bool ride = target_[Ride] > 0.5, makeup = target_[AutoMakeup] > 0.5;
    const int speed = static_cast<int>(target_[Speed] + 0.5);
    for (int off = 0; off < n; off += kChunk) {
        const int len = std::min(kChunk, n - off);
        // Ride: 400 ms loudness of the input toward Target, +-12 dB, held below -50 LUFS
        const float* in[2] = {ch[0] + off, nch > 1 ? ch[1] + off : ch[0] + off};
        meter_.process(in, nch, len);
        if (ride) {
            const double lufs = meter_.momentary();
            if (lufs > kRideGateLufs) {
                const double want = std::clamp(target_[Target] - lufs, -kRideMaxDb, kRideMaxDb);
                rideDb_ = want + rideC_ * (rideDb_ - want);
            }
        } else {
            rideDb_ *= rideC_;  // returns to 0 dB when switched off
        }
        const double rideFrom = prevRide_, rideTo = rideDb_;
        prevRide_ = rideTo;
        for (int k = 0; k < len; ++k) {
            const int i = off + k;
            const double rdb = rideFrom + (rideTo - rideFrom) * (k + 1) / len;
            const double rg = std::abs(rdb) < 1e-9 ? 1.0 : std::pow(10.0, rdb / 20.0);
            double x[2] = {0, 0}, level = 0;
            for (int c = 0; c < nch; ++c) {
                x[c] = ch[c][i] * rg;
                const double key = emphOn_ ? emph_[static_cast<size_t>(c)].process(x[c]) : x[c];
                level = std::max(level, det_[static_cast<size_t>(c)].process(key));
            }
            const double target = comp_.gainDb(20.0 * std::log10(std::max(level, 1e-9)));
            // two stages with the same 10 ms attack; the fast one releases quickly, the slow one slowly
            if (speed == 1) slowC_ = Ballistics::coef(fs_, 1000.0 * (1.0 + 2.0 * std::clamp(memory_ / 12.0, 0.0, 1.0)));
            grFast_ = target < grFast_ ? target + attackC_ * (grFast_ - target) : target + fastC_ * (grFast_ - target);
            grSlow_ = target < grSlow_ ? target + attackC_ * (grSlow_ - target) : target + slowC_ * (grSlow_ - target);
            gr_ = 0.5 * (grFast_ + grSlow_);
            const double depth = -gr_;   // the stretch memory builds up with depth and length
            memory_ = depth > memory_ ? depth + memAtt_ * (memory_ - depth) : depth + memRel_ * (memory_ - depth);
            avgGr_ = gr_ + avgC_ * (avgGr_ - gr_);
            makeupDb_ = makeup ? std::clamp(-avgGr_, 0.0, 24.0) : makeupDb_ * 0.9995;
            const double g = std::pow(10.0, (gr_ + makeupDb_) / 20.0) * rg;
            for (int c = 0; c < nch; ++c) {
                double y = static_cast<double>(ch[c][i]) * g;
                if (!std::isfinite(y)) y = 0.0;
                if (std::abs(y) < 1e-30) y = 0.0;
                ch[c][i] = static_cast<float>(y);
            }
        }
    }
}

}  // namespace sw::dy02
