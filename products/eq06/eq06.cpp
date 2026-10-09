#include "eq06/eq06.hpp"
#include <algorithm>
#include <cmath>

namespace sw::eq06 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<double> gains = {-12, -10, -8, -6, -4, -2, 0, 2, 4, 6, 8, 10, 12};
    static const std::vector<ParamSpec> s = {
        {"eq06.low.freq",  "Low Hz",    30, 400, 100,      Curve::Step, 1, {30, 50, 100, 200, 300, 400}, "Hz"},
        {"eq06.low.gain",  "Low Gain",  -12, 12, 0,        Curve::Step, 1, gains, "dB"},
        {"eq06.mid.freq",  "Mid kHz",   400, 8000, 1500,   Curve::Step, 1, {400, 800, 1500, 3000, 5000, 8000}, "Hz"},
        {"eq06.mid.gain",  "Mid Gain",  -12, 12, 0,        Curve::Step, 1, gains, "dB"},
        {"eq06.high.freq", "High kHz",  2500, 15000, 10000, Curve::Step, 1, {2500, 5000, 7500, 10000, 12500, 15000}, "Hz"},
        {"eq06.high.gain", "High Gain", -12, 12, 0,        Curve::Step, 1, gains, "dB"},
        {"eq06.shape",     "Shape",     0, 1, 0,           Curve::Step, 1, {0, 1}, "", {"Peak", "Shelf"}},
        {"eq06.drive",     "Drive",     0, 10, 2,          Curve::Lin,  1, {}, ""},
        {"eq06.out",       "Output",    -10, 10, 0,        Curve::Lin,  1, {}, "dB"},
        oversampleSpec("eq06.os"),
    };
    return s;
}

namespace {
constexpr int kControl = 16;
double propQ(double gainDb) { return 0.4 + std::max(0.0, std::abs(gainDb) - 2.0) * 0.11; }  // 0.4 @2 dB -> 1.5 @12 dB
double logHz(double hz) { return std::log(hz); }
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (LinearSmoother* s : {&lowG_, &midG_, &highG_, &lowF_, &midF_, &highF_, &drive_}) s->reset(fs_, 30.0, 0.0);  // Glide: 30 ms
    shelf_.reset(fs_, 10.0, 0.0);
    for (auto& c : ch_) { c = Ch{}; }
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
    snapToTargets();
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case LowHz: lowF_.setTarget(logHz(v)); break;
        case LowGain: lowG_.setTarget(v); break;
        case MidHz: midF_.setTarget(logHz(v)); break;
        case MidGain: midG_.setTarget(v); break;
        case HighHz: highF_.setTarget(logHz(v)); break;
        case HighGain: highG_.setTarget(v); break;
        case Shape: shelf_.setTarget(v); break;
        case Drive: drive_.setTarget(v * 1.8); break;
        case Oversample: for (auto& c : ch_) c.os.setFactor(static_cast<int>(v)); break;
        default: break;  // Output: sw::Shell
    }
}

void Processor::snapToTargets() {
    for (LinearSmoother* s : {&lowG_, &midG_, &highG_, &lowF_, &midF_, &highF_, &drive_, &shelf_}) s->skip(1 << 30);
    update(0);
}

void Processor::update(int ramp) {
    const double lf = std::exp(lowF_.current()), mf = std::exp(midF_.current()), hf = std::exp(highF_.current());
    const double lg = lowG_.current(), mg = midG_.current(), hg = highG_.current();
    for (auto& c : ch_) {
        c.lowPk.setupRamp(Svf::Mode::Bell, lf, fs_, propQ(lg), lg, ramp);
        c.lowSh.setupRamp(Svf::Mode::LowShelf, lf, fs_, 0.70710678, lg, ramp);
        c.mid.setupRamp(Svf::Mode::Bell, mf, fs_, propQ(mg), mg, ramp);
        c.highPk.setupRamp(Svf::Mode::Bell, hf, fs_, propQ(hg), hg, ramp);
        c.highSh.setupRamp(Svf::Mode::HighShelf, hf, fs_, 0.70710678, hg, ramp);
    }
    sat_.setHeadroom(2.0);  // +6 dBFS, same as every analog output stage (README)
    sat_.setDriveDb(drive_.current());
}

void Processor::process(float** chans, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    for (int start = 0; start < n; start += kControl) {
        const int len = std::min(kControl, n - start);
        bool moving = false;
        for (LinearSmoother* s : {&lowG_, &midG_, &highG_, &lowF_, &midF_, &highF_, &drive_}) if (s->isSmoothing()) { s->skip(len); moving = true; }
        if (moving) update(len);
        for (int i = start; i < start + len; ++i) {
            const double sh = shelf_.next();
            for (int c = 0; c < nch; ++c) {
                Ch& s = ch_[static_cast<size_t>(c)];
                double x = chans[c][i];
                const double lp = s.lowPk.process(x), ls = s.lowSh.process(x);
                x = lp + sh * (ls - lp);
                x = s.mid.process(x);
                const double hp = s.highPk.process(x), hs = s.highSh.process(x);
                x = hp + sh * (hs - hp);
                double y = s.os.process(x, [&](double u) { return sat_.process(u); });
                if (std::abs(y) < 1e-30) y = 0.0;
                chans[c][i] = static_cast<float>(y);
            }
        }
    }
}

}  // namespace sw::eq06
