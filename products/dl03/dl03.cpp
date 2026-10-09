#include "dl03/dl03.hpp"
#include "sw/notes.hpp"
#include <algorithm>
#include <cmath>

namespace sw::dl03 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"dl03.time",     "Time",      20, 600, 300, Curve::Log, 1, {}, "ms"},
        {"dl03.feedback", "Feedback",  0, 10, 4,     Curve::Lin, 1, {}, ""},
        {"dl03.moddepth", "Mod depth", 0, 10, 2,     Curve::Lin, 1, {}, ""},
        {"dl03.modrate",  "Mod rate",  0, 10, 3,     Curve::Lin, 1, {}, ""},
        {"dl03.grit",     "Grit",      0, 10, 2,     Curve::Lin, 1, {}, ""},
        {"dl03.mix",      "Mix",       0, 100, 25,   Curve::Lin, 1, {}, "%"},
        {"dl03.sync",     "Sync",      0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            unitSpec("dl03.unit"),
    };
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846;
double modRateHz(double v) { return 0.05 * std::pow(160.0, v / 10.0); }   // 0 .. 10 -> 0.05 .. 8 Hz
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

double Processor::timeSeconds() const {
    double t = target_[Time] * 0.001;
    if (target_[Sync] > 0.5 && bpm_ > 0.0) t = noteSeconds(noteNearest(t, bpm_), bpm_);
    return std::clamp(t, 0.020, 0.600);
}
double Processor::clockHz() const { return std::min(kStages / timeSeconds(), fs_); }

void Processor::setFilters(double clock) {   // `clock` is the unclamped 4096 / T: the filters keep following it above the host rate
    const double pre = std::min(clock * 0.4, 0.45 * fs_), post = std::min(clock * 0.2, 0.45 * fs_);
    for (auto& c : ch_) {
        c.pre1.setup(Svf::Mode::LowPass, pre, fs_, 0.7071, 0.0);
        c.post1.setup(Svf::Mode::LowPass, post, fs_, 0.7071, 0.0); c.post2.setup(Svf::Mode::LowPass, post, fs_, 0.7071, 0.0);
    }
    lastCut_ = clock;
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (size_t k = 0; k < 2; ++k) {
        auto& c = ch_[k];
        c.ring.assign(static_cast<size_t>(kStages) + 8, 0.0f); c.pos = 0; c.acc = 0.0; c.held = 0.0; c.env1 = c.env2 = 0.0; c.rng = 0x9e3779b9u * static_cast<unsigned>(k + 1);
        c.pre1.reset(); c.post1.reset(); c.post2.reset();
    }
    fbState_[0] = fbState_[1] = 0.0; ph_ = 0.0;
    t_ = timeSeconds();
    setFilters(kStages / timeSeconds());
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::snapToTargets() { if (!prepared_) return; t_ = timeSeconds(); setFilters(kStages / t_); }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const double fb = target_[Feedback] * 0.11;
    const double tgt = timeSeconds();
    const double glide = 1.0 - std::exp(-1.0 / (0.1 * fs_));
    const double depth = target_[ModDepth] * 0.1 * 0.003;   // seconds, +-3 ms at 10
    const double phInc = modRateHz(target_[ModRate]) / fs_;
    const double gritAmp = target_[Grit] * 0.1 * 0.01;     // noise in the bucket (compressed domain), 0 .. -40 dB
    const double envK = 1.0 - std::exp(-1.0 / (0.005 * fs_));
    for (int i = 0; i < n; ++i) {
        t_ += glide * (tgt - t_);
        ph_ += phInc; if (ph_ >= 1.0) ph_ -= 1.0;
        for (int c = 0; c < 2; ++c) {
            auto& ch2 = ch_[static_cast<size_t>(c)];
            double p = ph_ + (c == 0 ? 0.0 : 0.25); p -= std::floor(p);
            const double T = std::clamp(t_ + depth * std::sin(2.0 * kPi * p), 0.005, 0.620);
            const double clock = std::min(kStages / T, fs_);
            const double vclock = kStages / T;
            if (c == 0 && std::abs(vclock - lastCut_) > 0.01 * lastCut_) setFilters(vclock);
            const double in = c < nch ? ch[c][i] : ch[0][i];
            // loop: tanh limiter, compressor, clock-following pre-filter
            double x = std::tanh(in + fb * fbState_[static_cast<size_t>(c)]);
            ch2.env1 += envK * (std::abs(x) - ch2.env1);
            x /= std::sqrt(ch2.env1 + 0.003);
            x = ch2.pre1.process(x);
            // the bucket: a tick every fs / clock samples
            ch2.acc += clock / fs_;
            if (ch2.acc >= 1.0) {
                ch2.acc -= 1.0;
                ch2.rng = ch2.rng * 1664525u + 1013904223u;
                const double nz = ((ch2.rng >> 8) * (1.0 / 8388608.0) - 1.0);
                ch2.ring[ch2.pos] = static_cast<float>(x + gritAmp * nz * std::min(1.0, ch2.env1 * 300.0));   // the fizz exists only while there is signal to carry it
                ch2.pos = (ch2.pos + 1) % ch2.ring.size();
            }
            const size_t dTicks = static_cast<size_t>(std::clamp(T * clock, 1.0, static_cast<double>(kStages)));
            const size_t rp = (ch2.pos + ch2.ring.size() - 1 - std::min(dTicks, ch2.ring.size() - 2)) % ch2.ring.size();
            double y = ch2.post2.process(ch2.post1.process(static_cast<double>(ch2.ring[rp])));
            ch2.env2 += envK * (std::abs(y) - ch2.env2);
            y *= ch2.env2 + 0.003 * 0.0;
            fbState_[static_cast<size_t>(c)] = y;
            if (std::abs(y) < 1e-30) y = 0.0;
            if (c < nch) ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::dl03
