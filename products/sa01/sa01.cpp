#include "sa01/sa01.hpp"
#include <algorithm>
#include <cmath>

namespace sw::sa01 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"sa01.speed",      "Speed ips",  7.5, 30, 15,  Curve::Step, 1, {7.5, 15, 30}, "ips", {"7.5", "15", "30"}},
            {"sa01.formula",    "Formula",    0, 2, 0,      Curve::Step, 1, {0, 1, 2}, "", {"A", "B", "C"}},
            {"sa01.input",      "Input",      -12, 12, 0,   Curve::Lin,  1, {}, "dB"},
            {"sa01.saturation", "Saturation", 0, 10, 3,     Curve::Lin,  1, {}, ""},
            {"sa01.wow",        "Wow",        0, 10, 0,     Curve::Lin,  1, {}, ""},
            {"sa01.flutter",    "Flutter",    0, 10, 0,     Curve::Lin,  1, {}, ""},
            {"sa01.hiss",       "Hiss",       -90, -50, -90, Curve::Lin, 1, {}, "dBFS"},
            {"sa01.output",     "Output",     -10, 10, 0,   Curve::Lin,  1, {}, "dB"},
            {"sa01.repro",      "Repro",      0, 1, 1,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            oversampleSpec("sa01.os"),
            unitSpec("sa01.unit"),
        };
        v[Hiss].minLabel = "Off"; v[Hiss].maxLabel = "Max";
        return v;
    }();
    return s;
}

namespace {
constexpr double kRefLevel = 0.12589254117941673;       // 0 VU = -18 dBFS
constexpr double kHeadroom[3] = {1.0, 1.41, 0.71};      // Formula A / B / C: B +3 dB headroom, C -3 dB (design)
constexpr double kBumpHz[3] = {50.0, 70.0, 100.0}, kBumpDb[3] = {2.5, 2.0, 1.5};   // per speed 7.5 / 15 / 30 ips (design)
constexpr double kLossHz[3] = {12000.0, 16000.0, 22000.0};
constexpr double kCoerc = 0.08;                          // hysteresis loop half-width at Saturation 3 and a large level, in units of 0 VU (design)
constexpr double kPi = 3.14159265358979323846;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

int Processor::latencySamples() const { return std::max(1, static_cast<int>(std::lround(0.001 * fs_))); }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    centre_ = latencySamples();
    for (auto& r : ring_) r.assign(kRing, 0.0f);
    pos_ = 0;
    in_.reset(fs_, 20.0, std::pow(10.0, target_[Input] / 20.0)); out_.reset(fs_, 20.0, std::pow(10.0, target_[Output] / 20.0));
    for (auto& o : os_) o.reset();
    z_ = {0, 0}; zOs_ = {0, 0};
    envOs_ = {0, 0};
    applyOversample();
    wowPh_[0] = wowPh_[1] = 0; flPh_[0] = flPh_[1] = flPh_[2] = 0; drift_ = driftTarget_ = 0;
    rng_ = 0x2468ace1u;
    hissA_ = std::exp(-2.0 * kPi * 1500.0 / fs_);   // one-pole high-pass corner for the hiss tilt
    hissLp_[0] = hissLp_[1] = 0;
    // normalise the shaped noise to unit RMS (deterministic one-second run)
    { uint32_t r = 0x13579bdfu; double lp = 0, ss = 0; const int n = static_cast<int>(fs_);
      for (int i = 0; i < n; ++i) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; const double w = (r / 4294967296.0 - 0.5) * 3.4641016; lp = hissA_ * lp + (1.0 - hissA_) * w; const double y = 0.3 * w + (w - lp); ss += y * y; }
      hissNorm_ = 1.0 / std::sqrt(ss / n); }
    calLeft_ = 0; calPending_ = false;
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
    updateTone();
}

void Processor::updateTone() {
    const int sp = target_[Speed] < 11.0 ? 0 : target_[Speed] < 22.0 ? 1 : 2;
    const bool on = target_[Repro] > 0.5;
    for (int k = 0; k < 2; ++k) {   // each channel's head has its own tolerance (Unit B / C)
        auto& t = tone_[static_cast<size_t>(k)];
        t.bump.setup(Svf::Mode::Bell, kBumpHz[sp] * Unit::freqMul(unit_, k, 0), fs_, 1.0, on ? kBumpDb[sp] : 0.0);
        t.loss.setup(Svf::Mode::LowPass, std::min(kLossHz[sp] * Unit::freqMul(unit_, k, 1), fs_ * 0.45), fs_, 0.70710678, 0);
    }
}

void Processor::applyOversample() {
    for (auto& o : os_) o.setFactor(static_cast<int>(target_[Oversample]));
    envC_ = std::exp(-1.0 / (0.005 * os_[0].rate(fs_)));
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Input) in_.setTarget(std::pow(10.0, v / 20.0));
    else if (id == Output) out_.setTarget(std::pow(10.0, v / 20.0));
    else if (id == Speed || id == Repro) updateTone();
    else if (id == Oversample) applyOversample();
    else if (id == Unit) {
        unit_ = static_cast<int>(v);
        for (int k = 0; k < 2; ++k) onset_[static_cast<size_t>(k)] = std::pow(10.0, Unit::satDb(unit_, k, 0) / 20.0);
        updateTone();
    }
}

void Processor::startCalibrate() { calLeft_ = static_cast<long>(5.0 * fs_); calSum_ = 0; calCount_ = 0; calPending_ = false; }

int Processor::takeParamWrite(int& id, double& plain) {
    if (!calPending_) return 0;
    calPending_ = false; id = Input; plain = target_[Input];
    return 7;   // one-shot gesture: begin + value + end
}

double Processor::hermite(const std::vector<float>& r, double pos) const {
    const long i = static_cast<long>(std::floor(pos)); const double f = pos - static_cast<double>(i);
    auto at = [&](long k) { return static_cast<double>(r[static_cast<size_t>(((k % kRing) + kRing) % kRing)]); };
    const double y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const double c0 = y1, c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + c0;
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const int sp = target_[Speed] < 11.0 ? 0 : target_[Speed] < 22.0 ? 1 : 2;
    const double sat = target_[Saturation], g = 0.05 + 0.0527 * std::pow(sat, 1.34), ref = kRefLevel * kHeadroom[static_cast<int>(target_[Formula] + 0.5)];
    const bool repro = target_[Repro] > 0.5;
    const double widthScale = std::min(sat / 3.0, 2.0);   // no loop at Saturation 0, nominal at 3, at most twice that
    const double wowDepth = 0.04e-3 * target_[Wow] * fs_, flDepth = 0.0016e-3 * target_[Flutter] * fs_;   // samples of delay swing
    const double hissDb = target_[Hiss], hissAmp = hissDb > specs()[Hiss].min + 0.01 ? std::pow(10.0, hissDb / 20.0) * hissNorm_ * (sp == 0 ? 1.4 : sp == 1 ? 1.0 : 0.7) : 0.0;
    if (calLeft_ > 0) {   // Calibrate: mean level of the raw input over 5 s -> Input = -18 dBFS - mean (+-12 dB)
        for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) calSum_ += static_cast<double>(ch[c][i]) * ch[c][i];
        calCount_ += static_cast<long>(n) * nch; calLeft_ -= n;
        if (calLeft_ <= 0) {
            const double meanDb = calCount_ > 0 && calSum_ > 1e-20 ? 10.0 * std::log10(calSum_ / static_cast<double>(calCount_)) : -200.0;
            target_[Input] = std::clamp(-18.0 - meanDb, -12.0, 12.0); in_.setTarget(std::pow(10.0, target_[Input] / 20.0)); calPending_ = true;
        }
    }
    for (int i = 0; i < n; ++i) {
        const double gi = in_.next(), go = out_.next();
        // slow irregular drift + the wow / flutter phases
        if ((++driftCount_ & 255) == 0) driftTarget_ = ((rng_ = rng_ * 1664525u + 1013904223u) / 4294967296.0 - 0.5);
        drift_ += (driftTarget_ - drift_) * (1.0 / (0.35 * fs_));
        wowPh_[0] += 2.0 * kPi * 0.62 / fs_; wowPh_[1] += 2.0 * kPi * (1.37 + 0.4 * drift_) / fs_;
        flPh_[0] += 2.0 * kPi * 6.1 / fs_; flPh_[1] += 2.0 * kPi * (9.7 + 2.0 * drift_) / fs_; flPh_[2] += 2.0 * kPi * 14.3 / fs_;
        const double mod = wowDepth * (0.7 * std::sin(wowPh_[0]) + 0.3 * std::sin(wowPh_[1])) + flDepth * (0.5 * std::sin(flPh_[0]) + 0.3 * std::sin(flPh_[1]) + 0.2 * std::sin(flPh_[2]));
        for (int c = 0; c < nch; ++c) {
            rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
            const double white = (rng_ / 4294967296.0 - 0.5) * 3.4641016;   // unit variance
            // simplified hysteresis: the field is passed through a backlash (play operator) whose width grows with the recent level
            // (small signals see none: the bias linearises them), then through the anhysteretic curve M = tanh(g p) / g; as many samples as the oversampling setting says (2x by default)
            double& z = zOs_[static_cast<size_t>(c)]; double& env = envOs_[static_cast<size_t>(c)];
            const double gk = g * onset_[static_cast<size_t>(c)];
            const double sat1 = os_[static_cast<size_t>(c)].process(ch[c][i] * gi / ref, [&](double u) {
                env = std::max(std::abs(u), envC_ * env);
                const double w = widthScale * kCoerc * env * env / (1.0 + env * env);   // grows with the square of the recent level: nothing for small signals
                if (u > z + w) z = u - w; else if (u < z - w) z = u + w;
                return std::tanh(gk * z) / gk;
            }) * ref;
            auto& r = ring_[static_cast<size_t>(c)];
            r[static_cast<size_t>(pos_)] = static_cast<float>(sat1);
            double y = hermite(r, static_cast<double>(pos_) - centre_ - mod);
            if (repro) { auto& t = tone_[static_cast<size_t>(c)]; y = t.loss.process(t.bump.process(y)); }
            if (hissAmp > 0.0) { const double lp = hissA_ * hissLp_[c] + (1.0 - hissA_) * white; hissLp_[c] = lp; y += hissAmp * (0.3 * white + (white - lp)); }
            y *= go;
            if (!std::isfinite(y)) { y = 0.0; z_[static_cast<size_t>(c)] = 0; }
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
        pos_ = (pos_ + 1) & (kRing - 1);
    }
}

}  // namespace sw::sa01
