#include "cs03/cs03.hpp"
#include <algorithm>
#include <cmath>

namespace sw::cs03 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<double> g = {-12, -10, -8, -6, -4, -2, 0, 2, 4, 6, 8, 10, 12};
    static const std::vector<ParamSpec> s = {
        {"cs03.pre.gain",    "Gain",      0, 60, 30,   Curve::Lin,  1, {}, ""},
        {"cs03.pre.z",       "Impedance", 0, 1, 0,     Curve::Step, 1, {0, 1}, "", {"Lo Z", "Hi Z"}},
        {"cs03.eq.low",      "Low",       -12, 12, 0,  Curve::Step, 1, g, "dB"},
        {"cs03.eq.mid",      "Mid",       -12, 12, 0,  Curve::Step, 1, g, "dB"},
        {"cs03.eq.high",     "High",      -12, 12, 0,  Curve::Step, 1, g, "dB"},
        {"cs03.comp.thresh", "Thresh",    0, 10, 0,    Curve::Lin,  1, {}, ""},
        {"cs03.comp.ratio",  "Ratio",     1.2, 10, 2,  Curve::Log,  1, {}, ":1"},
        {"cs03.comp.knee",   "Knee",      0, 1, 1,     Curve::Step, 1, {0, 1}, "", {"Hard", "Soft"}},
        {"cs03.out",         "Output",    -10, 10, 0,  Curve::Lin,  1, {}, "dB"},
        oversampleSpec("cs03.os"),
    };
    return s;
}

namespace {
constexpr int kControl = 16;
constexpr double kHeadroom = 2.0;      // +6 dBFS like every analog stage in the bundle
constexpr double kLowHeadroom = 1.2;   // design: the core saturates the lows first
constexpr double kHiZEven = 0.15;      // design: asymmetry for even harmonics on Hi Z
constexpr double kHiZLoadDb = -2.5;    // design: instrument-input load on the top end (8 kHz shelf)
double tf(double v, double h) { return h * std::tanh(v / h); }
double propQ(double g) { return 0.4 + std::max(0.0, std::abs(g) - 2.0) * 0.11; }  // EQ06 proportional Q
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    gain_.reset(fs_, 20.0, 1.0);
    hiZ_.reset(fs_, 10.0, 0.0);
    for (LinearSmoother* s : {&low_, &mid_, &high_}) s->reset(fs_, 30.0, 0.0);  // Glide 30 ms
    for (auto& c : ch_) {
        c = Ch{};
        c.dc.setup(OnePole::Mode::HighPass, 10.0, fs_);
        c.load.setup(Svf::Mode::HighShelf, std::min(8000.0, 0.45 * fs_), fs_, 0.70710678, kHiZLoadDb);
        c.det.set(fs_, LevelDetector::Mode::Program);
    }
    fast_.reset(0); slow_.reset(0);
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
    snapToTargets();
}

// the transformer's low split is a filter inside the oversampled loop: its coefficient is for the oversampled rate (the common setting, default 2x)
void Processor::updateSplit() { for (auto& c : ch_) c.split.setup(OnePole::Mode::LowPass, 150.0, c.os.rate(fs_)); }

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case Gain: gain_.setTarget(std::pow(10.0, (v - 30.0) / 20.0)); break;
        case Impedance: hiZ_.setTarget(v); break;
        case Low: low_.setTarget(v); break;
        case Mid: mid_.setTarget(v); break;
        case High: high_.setTarget(v); break;
        case Thresh: case Ratio: case Knee: gc_.set(-4.0 * target_[Thresh], target_[Ratio], target_[Knee] > 0.5 ? 6.0 : 0.0); break;
        case Oversample: for (auto& ch : ch_) ch.os.setFactor(static_cast<int>(v)); updateSplit(); break;
        default: break;  // Output: sw::Shell
    }
}

void Processor::snapToTargets() {
    for (LinearSmoother* s : {&gain_, &hiZ_, &low_, &mid_, &high_}) s->skip(1 << 30);
    slow_.set(fs_, 1000.0, 1200.0);
    updateEq(0);
}

void Processor::updateEq(int ramp) {
    for (auto& c : ch_) {
        c.low.setupRamp(Svf::Mode::LowShelf, 100.0, fs_, 0.70710678, low_.current(), ramp);
        c.mid.setupRamp(Svf::Mode::Bell, 1500.0, fs_, propQ(mid_.current()), mid_.current(), ramp);
        c.high.setupRamp(Svf::Mode::HighShelf, std::min(10000.0, 0.45 * fs_), fs_, 0.70710678, high_.current(), ramp);
    }
}

void Processor::process(float** chans, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    for (int start = 0; start < n; start += kControl) {
        const int len = std::min(kControl, n - start);
        bool moving = false;
        for (LinearSmoother* s : {&low_, &mid_, &high_}) if (s->isSmoothing()) { s->skip(len); moving = true; }
        if (moving) updateEq(len);
        for (int i = start; i < start + len; ++i) {
            const double g = gain_.next(), hz = hiZ_.next();
            double x[2] = {0, 0}, level = 0;
            for (int k = 0; k < nch; ++k) {
                Ch& c = ch_[static_cast<size_t>(k)];
                double y = c.os.process(chans[k][i] * g, [&](double u) {  // transformer: lows saturate first; Hi Z adds asymmetry (even harmonics)
                    const double v = u + hz * kHiZEven * u * u / kHeadroom;
                    const double lo = c.split.process(v);
                    return tf(lo, kLowHeadroom) + tf(v - lo, kHeadroom);
                });
                if (hz > 0.0) y = c.dc.process(y) * hz + y * (1.0 - hz);  // the asymmetry's DC is removed on Hi Z
                if (hz > 0.0) y = y + hz * (c.load.process(y) - y);       // instrument load on the top end
                y = c.high.process(c.mid.process(c.low.process(y)));
                x[k] = y;
                level = std::max(level, c.det.process(y));
            }
            // feed-forward compressor, program-dependent attack, two-stage auto release (linked)
            const double lv = 20.0 * std::log10(std::max(level, 1e-9));
            const double target = gc_.gainDb(lv), over = std::max(0.0, lv - gc_.t_);
            fast_.set(fs_, std::clamp(30.0 / (1.0 + over / 6.0), 3.0, 30.0), 100.0);
            const double gr = std::min(fast_.process(target), slow_.process(target));
            const double cg = gr == 0.0 ? 1.0 : std::pow(10.0, gr / 20.0);
            for (int k = 0; k < nch; ++k) {
                const double y = x[k] * cg;
                chans[k][i] = static_cast<float>(std::abs(y) < 1e-30 ? 0.0 : y);
            }
        }
    }
}

}  // namespace sw::cs03
