#include "eq05/eq05.hpp"
#include <cmath>

namespace sw::eq05 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"eq05.hf.gain",    "HF Gain",   -15, 15, 0,       Curve::Lin, 1, {}, "dB"},
        {"eq05.hf.freq",    "HF Freq",   1500, 16000, 8000, Curve::Log, 1, {}, "Hz"},
        {"eq05.hf.shape",   "HF Shape",  0, 1, 0,          Curve::Step, 1, {0, 1}, "", {"Shelf", "Bell"}},
        {"eq05.hmf.gain",   "HMF Gain",  -15, 15, 0,       Curve::Lin, 1, {}, "dB"},
        {"eq05.hmf.freq",   "HMF Freq",  600, 7000, 2000,  Curve::Log, 1, {}, "Hz"},
        {"eq05.hmf.q",      "HMF Q",     0.5, 3, 1,        Curve::Log, 1, {}, ""},
        {"eq05.lmf.gain",   "LMF Gain",  -15, 15, 0,       Curve::Lin, 1, {}, "dB"},
        {"eq05.lmf.freq",   "LMF Freq",  200, 2500, 600,   Curve::Log, 1, {}, "Hz"},
        {"eq05.lmf.q",      "LMF Q",     0.5, 3, 1,        Curve::Log, 1, {}, ""},
        {"eq05.lf.gain",    "LF Gain",   -15, 15, 0,       Curve::Lin, 1, {}, "dB"},
        {"eq05.lf.freq",    "LF Freq",   30, 450, 100,     Curve::Log, 1, {}, "Hz"},
        {"eq05.lf.shape",   "LF Shape",  0, 1, 0,          Curve::Step, 1, {0, 1}, "", {"Shelf", "Bell"}},
        {"eq05.hpf",        "HPF",       0, 200, 0,        Curve::Step, 1, {0, 40, 80, 120, 200}, "Hz", {"Off", "40 Hz", "80 Hz", "120 Hz", "200 Hz"}},
        {"eq05.lpf",        "LPF",       0, 20000, 0,      Curve::Step, 1, {0, 8000, 12000, 16000, 20000}, "Hz", {"Off", "8 kHz", "12 kHz", "16 kHz", "20 kHz"}},
        {"eq05.drive",      "Drive",     0, 10, 2,         Curve::Lin, 1, {}, ""},
        {"eq05.drive.pos",  "Drive Pos", 0, 1, 1,          Curve::Step, 1, {0, 1}, "", {"Pre", "Post"}},
        {"eq05.out",        "Output",    -10, 10, 0,       Curve::Lin, 1, {}, "dB"},
        {"eq05.in",         "In",        0, 1, 1,          Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

namespace {
constexpr int kControlRate = 16;     // coefficient update interval while smoothing (samples)
constexpr double kRampMs = 20.0, kFadeMs = 10.0;
constexpr double kShelfBellQ = 0.7, kButterQ = 0.70710678118654752, kHpfStageQ = 1.0;
}

Processor::Processor() {
    for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def;
}

void Processor::prepare(double sampleRate, int /*maxBlock*/) {
    fs_ = sampleRate;
    fadeLength_ = std::max(1, static_cast<int>(std::lround(fs_ * kFadeMs * 0.001)));
    ch_ = {}; fade_ = {}; fadeRemaining_ = 0;
    auto r = [&](LinearSmoother& s, double ms) { s.reset(fs_, ms, 0.0); };
    for (LinearSmoother* s : {&ctl_.hfGain, &ctl_.hfFreqN, &ctl_.hmfGain, &ctl_.hmfFreqN, &ctl_.hmfQN, &ctl_.lmfGain, &ctl_.lmfFreqN,
                              &ctl_.lmfQN, &ctl_.lfGain, &ctl_.lfFreqN, &ctl_.hpfFreqN, &ctl_.lpfFreqN, &ctl_.drive}) r(*s, kRampMs);
    for (LinearSmoother* s : {&ctl_.hfBell, &ctl_.lfBell, &ctl_.hpfOn, &ctl_.lpfOn}) r(*s, kFadeMs);
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
    snapToTargets();
}

// HPF/LPF cutoff smoothing happens in the log domain over the full audible range
static double hpfNorm(double hz) { return std::log(hz / 20.0) / std::log(1000.0); }
static double hpfHz(double n) { return 20.0 * std::pow(1000.0, n); }

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));  // clamp / snap to the legal grid
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case HfGain:  ctl_.hfGain.setTarget(v); break;
        case HfFreq:  ctl_.hfFreqN.setTarget(sp.toNorm(v)); break;
        case HfShape: ctl_.hfBell.setTarget(v); break;
        case HmfGain: ctl_.hmfGain.setTarget(v); break;
        case HmfFreq: ctl_.hmfFreqN.setTarget(sp.toNorm(v)); break;
        case HmfQ:    ctl_.hmfQN.setTarget(sp.toNorm(v)); break;
        case LmfGain: ctl_.lmfGain.setTarget(v); break;
        case LmfFreq: ctl_.lmfFreqN.setTarget(sp.toNorm(v)); break;
        case LmfQ:    ctl_.lmfQN.setTarget(sp.toNorm(v)); break;
        case LfGain:  ctl_.lfGain.setTarget(v); break;
        case LfFreq:  ctl_.lfFreqN.setTarget(sp.toNorm(v)); break;
        case LfShape: ctl_.lfBell.setTarget(v); break;
        case Hpf:     ctl_.hpfOn.setTarget(v > 0 ? 1.0 : 0.0); if (v > 0) ctl_.hpfFreqN.setTarget(hpfNorm(v)); break;
        case Lpf:     ctl_.lpfOn.setTarget(v > 0 ? 1.0 : 0.0); if (v > 0) ctl_.lpfFreqN.setTarget(hpfNorm(v)); break;
        case Drive:   ctl_.drive.setTarget(v * 1.8); break;  // 0..10 -> 0..+18 dB
        case DrivePos: {
            const int pos = static_cast<int>(v);
            if (pos != drivePos_) { fade_ = ch_; fadePos_ = drivePos_; fadeRemaining_ = fadeLength_; drivePos_ = pos; }
            break;
        }
        case Output: case In: break;  // handled by sw::Shell (common frame)
        default: break;
    }
}

void Processor::snapToTargets() {
    for (LinearSmoother* s : {&ctl_.hfGain, &ctl_.hfFreqN, &ctl_.hmfGain, &ctl_.hmfFreqN, &ctl_.hmfQN, &ctl_.lmfGain, &ctl_.lmfFreqN, &ctl_.lmfQN,
                              &ctl_.lfGain, &ctl_.lfFreqN, &ctl_.hpfFreqN, &ctl_.lpfFreqN, &ctl_.drive,
                              &ctl_.hfBell, &ctl_.lfBell, &ctl_.hpfOn, &ctl_.lpfOn})
        s->skip(1 << 30);
    fadeRemaining_ = 0;
    updateCoefficients();
}

bool Processor::anySmoothing() const {
    for (const LinearSmoother* s : {&ctl_.hfGain, &ctl_.hfFreqN, &ctl_.hmfGain, &ctl_.hmfFreqN, &ctl_.hmfQN, &ctl_.lmfGain, &ctl_.lmfFreqN,
                                    &ctl_.lmfQN, &ctl_.lfGain, &ctl_.lfFreqN, &ctl_.hpfFreqN, &ctl_.lpfFreqN, &ctl_.drive})
        if (s->isSmoothing()) return true;
    return false;
}

void Processor::updateCoefficients(int n) {
    const auto& s = specs();
    const double hf = s[HfFreq].toValue(ctl_.hfFreqN.current()), hmf = s[HmfFreq].toValue(ctl_.hmfFreqN.current()),
                 lmf = s[LmfFreq].toValue(ctl_.lmfFreqN.current()), lf = s[LfFreq].toValue(ctl_.lfFreqN.current());
    const double hmfQ = s[HmfQ].toValue(ctl_.hmfQN.current()), lmfQ = s[LmfQ].toValue(ctl_.lmfQN.current());
    const double hpf = hpfHz(ctl_.hpfFreqN.current()), lpf = hpfHz(ctl_.lpfFreqN.current());
    // n > 1: move coefficients linearly over the next n samples (no zipper); otherwise jump
    for (Chain* c : {&ch_[0], &ch_[1], &fade_[0], &fade_[1]}) {
        c->hfShelf.setupRamp(Svf::Mode::HighShelf, hf, fs_, kButterQ, ctl_.hfGain.current(), n);
        c->hfBell.setupRamp(Svf::Mode::Bell, hf, fs_, kShelfBellQ, ctl_.hfGain.current(), n);
        c->hmf.setupRamp(Svf::Mode::Bell, hmf, fs_, hmfQ, ctl_.hmfGain.current(), n);
        c->lmf.setupRamp(Svf::Mode::Bell, lmf, fs_, lmfQ, ctl_.lmfGain.current(), n);
        c->lfShelf.setupRamp(Svf::Mode::LowShelf, lf, fs_, kButterQ, ctl_.lfGain.current(), n);
        c->lfBell.setupRamp(Svf::Mode::Bell, lf, fs_, kShelfBellQ, ctl_.lfGain.current(), n);
        c->hpf1.setupRamp(OnePole::Mode::HighPass, hpf, fs_, n);
        c->hpf2.setupRamp(Svf::Mode::HighPass, hpf, fs_, kHpfStageQ, 0, n);
        c->lpf.setupRamp(Svf::Mode::LowPass, lpf, fs_, kButterQ, 0, n);
    }
    sat_.setHeadroom(2.0);  // +6 dBFS, same as every analog output stage (README)
    sat_.setDriveDb(ctl_.drive.current());
}

// all alternatives run continuously so that switching only crossfades valid outputs
double Processor::eq(Chain& c, double x) const {
    const double h = c.hpf2.process(c.hpf1.process(x));
    x += hpfOn_ * (h - x);
    const double l = c.lpf.process(x);
    x += lpfOn_ * (l - x);
    const double ls = c.lfShelf.process(x), lb = c.lfBell.process(x);
    x = ls + lfBell_ * (lb - ls);
    x = c.lmf.process(x);
    x = c.hmf.process(x);
    const double hs = c.hfShelf.process(x), hb = c.hfBell.process(x);
    return hs + hfBell_ * (hb - hs);
}

double Processor::drive(Chain& c, double x) const {
    double up[2];
    c.os.up(x, up);
    up[0] = sat_.process(up[0]);
    up[1] = sat_.process(up[1]);
    return c.os.down(up);
}

double Processor::runChain(Chain& c, double x, int pos) const {
    return pos == 0 ? eq(c, drive(c, x)) : drive(c, eq(c, x));
}

void Processor::process(float** chans, int numCh, int n) {
    numCh = std::min(numCh, 2);
    for (int start = 0; start < n; start += kControlRate) {
        const int len = std::min(kControlRate, n - start);
        if (anySmoothing()) {
            for (LinearSmoother* s : {&ctl_.hfGain, &ctl_.hfFreqN, &ctl_.hmfGain, &ctl_.hmfFreqN, &ctl_.hmfQN, &ctl_.lmfGain, &ctl_.lmfFreqN,
                                      &ctl_.lmfQN, &ctl_.lfGain, &ctl_.lfFreqN, &ctl_.hpfFreqN, &ctl_.lpfFreqN, &ctl_.drive})
                s->skip(len);
            updateCoefficients(len);
        }
        for (int i = start; i < start + len; ++i) {
            hfBell_ = ctl_.hfBell.next(); lfBell_ = ctl_.lfBell.next();
            hpfOn_ = ctl_.hpfOn.next();   lpfOn_ = ctl_.lpfOn.next();
            double xf = 0;
            if (fadeRemaining_ > 0) xf = static_cast<double>(fadeRemaining_--) / fadeLength_;
            for (int c = 0; c < numCh; ++c) {
                const double x = chans[c][i];
                double y = runChain(ch_[static_cast<size_t>(c)], x, drivePos_);
                if (xf > 0) y += xf * (runChain(fade_[static_cast<size_t>(c)], x, fadePos_) - y);
                if (std::abs(y) < 1e-30) y = 0.0;  // below -600 dBFS: flush, never emit subnormals
                chans[c][i] = static_cast<float>(y);
            }
        }
    }
}

}  // namespace sw::eq05
