#include "md01/md01.hpp"
#include <algorithm>
#include <cmath>

namespace sw::md01 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"md01.mode",  "Mode",  0, 2, 1,     Curve::Step, 1, {0, 1, 2}, "", {"I", "II", "I+II"}},
        {"md01.rate",  "Rate",  0.1, 5, 0.5, Curve::Log, 1, {}, "Hz"},
        {"md01.depth", "Depth", 0, 10, 5,    Curve::Lin, 1, {}, ""},
        {"md01.width", "Width", 0, 100, 100, Curve::Lin, 1, {}, "%"},
        {"md01.tone",  "Tone",  0, 100, 50,  Curve::Lin, 1, {}, "%"},
        {"md01.mix",   "Mix",   0, 100, 50,  Curve::Lin, 1, {}, "%"},
    };
    return s;
}
namespace {
constexpr double kCentreMs = 7.0, kDevMs = 3.0, kRate2 = 1.6, kHissAmp = 0.000126;   // -78 dBFS rms (uniform noise: amplitude x sqrt(3) -> 1 rms)
double tri(double p) { p -= std::floor(p); return p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p; }   // -1 .. 1, starts at -1
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::setTone() {
    const double cut = 2500.0 * std::pow(14000.0 / 2500.0, target_[Tone] * 0.01);
    for (auto& c : ch_) { c.lp.setup(Svf::Mode::LowPass, std::min(cut, 0.45 * fs_), fs_, 0.7071, 0.0); c.hp.setup(Svf::Mode::HighPass, 120.0, fs_, 0.7071, 0.0); }
    toneState_ = target_[Tone];
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    size_t sz = 16; while (sz < static_cast<size_t>(0.05 * fs_) + 16) sz <<= 1;
    mask_ = sz - 1;
    for (auto& b : buf_) b.assign(sz, 0.0f);
    for (auto& c : ch_) { c.lp.reset(); c.hp.reset(); }
    pos_ = 0; ph_ = 0.0; ph2_ = 0.0; env_ = 0.0;
    setTone();
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::snapToTargets() { if (prepared_) setTone(); }

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
    if (target_[Tone] != toneState_) setTone();
    const int mode = static_cast<int>(target_[Mode] + 0.5);
    const double depth = target_[Depth] * 0.1, rate = target_[Rate];
    const double devI = depth * kDevMs * 0.6 * 0.001 * fs_, devII = depth * kDevMs * 1.0 * 0.001 * fs_, centre = kCentreMs * 0.001 * fs_;
    const double inc1 = rate / fs_, inc2 = rate * kRate2 / fs_;
    const double rOff = target_[Width] * 0.01 * 0.5;   // right channel's phase offset in cycles: 0 .. 0.5 (180 degrees)
    const double envK = 1.0 - std::exp(-1.0 / (0.01 * fs_));
    const double g = mode == I_II ? 0.70710678118654752 : 1.0;
    for (int i = 0; i < n; ++i) {
        ph_ += inc1; if (ph_ >= 1.0) ph_ -= 1.0;
        ph2_ += inc2; if (ph2_ >= 1.0) ph2_ -= 1.0;
        double in[2] = {ch[0][i], nch > 1 ? ch[1][i] : ch[0][i]};
        const double am = std::max(std::abs(in[0]), std::abs(in[1]));
        env_ += envK * (am - env_);
        for (int c = 0; c < 2; ++c) buf_[static_cast<size_t>(c)][pos_ & mask_] = static_cast<float>(in[c]);
        ++pos_;
        double out[2] = {0.0, 0.0};
        for (int c = 0; c < 2; ++c) {
            const double off = c == 0 ? 0.0 : rOff;
            double y = 0.0;
            if (mode == I || mode == I_II) { const double d = centre + devI * tri(ph_ + off); lastDelay_[c][0] = d; y += g * read(c, d); }
            if (mode == II || mode == I_II) { const double d = centre + devII * tri(ph2_ + off + (mode == I_II ? 0.25 : 0.0)); lastDelay_[c][1] = d; y += g * read(c, d); }
            auto& cc = ch_[static_cast<size_t>(c)];
            rng_ = rng_ * 1664525u + 1013904223u;
            const double nz = ((rng_ >> 8) * (1.0 / 8388608.0) - 1.0) * kHissAmp * 1.7320508 * std::min(1.0, env_ * 300.0);
            y = cc.hp.process(cc.lp.process(y + nz));
            out[c] = y;
        }
        for (int c = 0; c < nch; ++c) { double y = out[c]; if (std::abs(y) < 1e-30) y = 0.0; ch[c][i] = static_cast<float>(y); }
    }
}

}  // namespace sw::md01
