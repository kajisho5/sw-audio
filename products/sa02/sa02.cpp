#include "sa02/sa02.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

namespace sw::sa02 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"sa02.color",     "Color",     0, 3, 0,      Curve::Step, 1, {0, 1, 2, 3}, "", {"Iron", "Clean", "Punch", "Vint"}},
            {"sa02.drive",     "Drive",     0, 10, 2,     Curve::Lin,  1, {}, ""},
            {"sa02.crosstalk", "Crosstalk", 0, 10, 0,     Curve::Lin,  1, {}, ""},
            {"sa02.noise",     "Noise",     0, 1, 0,      Curve::Lin,  1, {}, ""},
            {"sa02.width",     "Width",     0, 150, 100,  Curve::Lin,  1, {}, "%"},
            {"sa02.output",    "Output",    -10, 10, 0,   Curve::Lin,  1, {}, "dB"},
            {"sa02.group",     "Group",     1, 8, 1,      Curve::Step, 1, {1, 2, 3, 4, 5, 6, 7, 8}, ""},
        };
        v[Noise].minLabel = "Off"; v[Noise].maxLabel = "Max";
        return v;
    }();
    return s;
}

namespace {
constexpr int kGroups = 8, kSlots = 64;
std::atomic<float> gMs[kGroups][kSlots];     // mean square of each instance of a group (process-wide, lock-free)
std::atomic<bool> gUsed[kGroups][kSlots];
std::atomic<uint32_t> gCounter{1};
// colour: extra drive on the lows (dB), bias, headroom, drive scale
struct Col { double lfDb, bias, headroom, scale; };
constexpr Col kCol[4] = {{6.0, 0.05, 2.0, 1.0}, {0.0, 0.0, 2.0, 0.5}, {0.0, 0.0, 1.5, 1.0}, {3.0, 0.45, 2.0, 1.0}};
constexpr double kLowSplitHz = 160.0, kPi = 3.14159265358979323846;
double unit(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s / 4294967296.0 * 2.0 - 1.0; }
}

Processor::Processor() {
    for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def;
    const uint32_t t = static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());   // a new instance gets its own seed
    seed_ = 1 + static_cast<int>(((gCounter.fetch_add(1) * 2654435761u) ^ t ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this) >> 4)) % 65535u);
}
void Processor::Slot::release() { if (idx >= 0) { gMs[group - 1][idx].store(0.0f); gUsed[group - 1][idx].store(false); idx = -1; } }

void Processor::joinGroup() {
    slot_.release();
    slot_.group = std::clamp(static_cast<int>(target_[Group] + 0.5), 1, kGroups);
    for (int i = 0; i < kSlots; ++i) { bool exp = false; if (gUsed[slot_.group - 1][i].compare_exchange_strong(exp, true)) { slot_.idx = i; gMs[slot_.group - 1][i].store(0.0f); break; } }
}

void Processor::applySeed() {
    uint32_t r = static_cast<uint32_t>(seed_) * 2654435761u + 0x1234567u; if (r == 0) r = 1;
    for (int k = 0; k < 4; ++k) unit(r);
    devGain_ = {0.3 * unit(r), 0.3 * unit(r)};      // dB
    devDrive_ = {0.5 * unit(r), 0.5 * unit(r)};     // dB (where the saturation starts)
    devNoise_ = 1.0 * unit(r);                      // dB
    const double corner = 1.0 + 0.03 * unit(r);     // tone corners
    for (size_t c = 0; c < 2; ++c) {
        tone_[c].shelf.setup(Svf::Mode::HighShelf, 6000.0 * corner, fs_, 0.70710678, target_[Color] > 1.5 && target_[Color] < 2.5 ? 1.2 : 0.0);
        tone_[c].bump.setup(Svf::Mode::Bell, 60.0 * corner, fs_, 0.9, target_[Color] > 2.5 ? 1.5 : 0.0);
        tone_[c].lowpass.setup(Svf::Mode::LowPass, std::min(target_[Color] > 2.5 ? 13000.0 * corner : 0.45 * fs_, 0.45 * fs_), fs_, 0.70710678, 0);
    }
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    out_.reset(fs_, 20.0, std::pow(10.0, target_[Output] / 20.0));
    lf_.prepare(fs_); hf_.prepare(fs_);
    lp_ = {0, 0}; lpA_ = 1.0 - std::exp(-2.0 * kPi * kLowSplitHz / fs_);
    msC_ = std::exp(-1.0 / (0.1 * fs_)); msSmooth_ = 0; loadDb_ = 0;
    rng_ = static_cast<uint32_t>(seed_) * 747796405u + 2891336453u;
    applySeed();
    joinGroup();
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Output) out_.setTarget(std::pow(10.0, v / 20.0));
    else if (id == Color) applySeed();
    else if (id == Group && slot_.idx >= 0) joinGroup();
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const int color = static_cast<int>(target_[Color] + 0.5);
    const Col& col = kCol[color];
    const double driveDb = target_[Drive] * 1.5 * col.scale;
    const double leak = std::pow(10.0, (-80.0 + 4.0 * target_[Crosstalk]) / 20.0), width = target_[Width] / 100.0;
    const double noiseAmp = target_[Noise] > 1e-6 ? std::pow(10.0, (-100.0 + 30.0 * target_[Noise] + devNoise_) / 20.0) : 0.0;
    // bus loading from the other instances of the group (their summed mean square), smoothed per block
    double others = 0;
    if (slot_.idx >= 0) for (int i = 0; i < kSlots; ++i) if (i != slot_.idx && gUsed[slot_.group - 1][i].load(std::memory_order_relaxed)) others += gMs[slot_.group - 1][i].load(std::memory_order_relaxed);
    const double wantLoad = 3.0 * std::tanh(std::sqrt(others) / 0.3);
    loadDb_ += (wantLoad - loadDb_) * (1.0 - std::pow(msC_, n));
    double ms = 0;
    for (int i = 0; i < n; ++i) {
        const double go = out_.next();
        double y[2] = {0, 0};
        for (int c = 0; c < nch; ++c) {
            const size_t cc = static_cast<size_t>(c);
            const double x = ch[c][i] * std::pow(10.0, devGain_[cc] / 20.0);
            const double gDb = driveDb + loadDb_ * col.scale + devDrive_[cc];
            const double g = std::pow(10.0, std::max(gDb, -6.0) / 20.0);
            lp_[cc] += lpA_ * (x - lp_[cc]);
            const double lo = lp_[cc], hi = x - lo;
            const double gLo = g * std::pow(10.0, col.lfDb * std::min(1.0, target_[Drive] / 4.0) / 20.0);
            double v = lf_.process(c, lo, gLo, col.bias, col.headroom) + hf_.process(c, hi, g, col.bias * 0.7, col.headroom);
            Tone& t = tone_[cc];
            if (color == 2) v = t.shelf.process(v); else if (color == 3) v = t.lowpass.process(t.bump.process(v));
            y[c] = v;
        }
        if (nch > 1) {
            const double l = y[0] + leak * y[1], r = y[1] + leak * y[0];
            const double m = 0.5 * (l + r), s = 0.5 * (l - r) * width;
            y[0] = m + s; y[1] = m - s;
        }
        for (int c = 0; c < nch; ++c) {
            double v = y[c];
            if (noiseAmp > 0.0) v += noiseAmp * unit(rng_) * 1.7320508;
            v *= go;
            if (!std::isfinite(v)) v = 0.0;
            if (std::abs(v) < 1e-30) v = 0.0;
            ch[c][i] = static_cast<float>(v);
            ms += static_cast<double>(v) * v;
        }
    }
    if (slot_.idx >= 0 && n > 0) {
        msSmooth_ = std::pow(msC_, n) * msSmooth_ + (1.0 - std::pow(msC_, n)) * ms / (n * std::max(1, nch));
        gMs[slot_.group - 1][slot_.idx].store(static_cast<float>(msSmooth_), std::memory_order_relaxed);
    }
}

}  // namespace sw::sa02
