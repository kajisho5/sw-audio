#include "lv25/lv25.hpp"
#include "sw/tail.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv25 {
namespace { constexpr double kTone[3] = {3000.0, 6000.0, 12000.0}; }

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv25.clock",    "Clock",    0, 2, 0,    Curve::Step, 1, {0, 1, 2}, "", {"Tap", "MIDI", "BPM"}},
        {"lv25.time",     "Time",     1, 2000, 500, Curve::Log, 1, {}, "ms"},
        {"lv25.feedback", "Feedback", 0, 95, 35,  Curve::Lin, 1, {}, "%"},
        {"lv25.tone",     "Tone",     0, 2, 0,    Curve::Step, 1, {0, 1, 2}, "", {"Dark", "Neutral", "Bright"}},
        {"lv25.mix",      "Mix",      0, 100, 15, Curve::Lin, 1, {}, "%"},
        {"lv25.bypass",   "Input bypass", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; timeS_ = target_[Time] * 0.001; tapTime_ = timeS_; }

double Processor::goalS() const {
    switch (static_cast<int>(target_[Clock] + 0.5)) {
        case Tap: return std::clamp(tapTime_, 0.001, 2.0);
        case Bpm: return bpm_ > 20.0 ? std::clamp(60.0 / bpm_, 0.001, 2.0) : target_[Time] * 0.001;
        default: return midiBpm_ > 20.0 ? std::clamp(60.0 / midiBpm_, 0.001, 2.0) : target_[Time] * 0.001;
    }
}

// the repeats: one trip round the loop per delay time (the time the clock gives), each Feedback of the one before
double Processor::tailSeconds() const { return tail::loop(std::max(timeS_, goalS()), target_[Feedback] * 0.01) + 0.3; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate; size_t sz = 16; while (sz < static_cast<size_t>(2.1 * fs_) + 16) sz <<= 1;
    for (auto& b : buf_) b.assign(sz, 0.0f); mask_ = sz - 1; pos_ = 0; echo_.fill(0.0); clock_ = 0; lastTap_ = -1e9; tapCount_ = 0; write_ = false;
    for (auto& c : c_) { c.hp.setup(Svf::Mode::HighPass, 80.0, fs_, 0.70710678, 0); c.lp.setup(Svf::Mode::LowPass, std::min(kTone[static_cast<int>(target_[Tone] + 0.5)], fs_ * 0.45), fs_, 0.70710678, 0); }
    tapTime_ = target_[Time] * 0.001; timeS_ = goalS(); prepared_ = true;
}
void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v));
    if (id == Time) { tapTime_ = target_[Time] * 0.001; write_ = false; }   // the knob and the tap clock are one value
    if (id == Tone && prepared_) for (auto& c : c_) c.lp.setup(Svf::Mode::LowPass, std::min(kTone[static_cast<int>(target_[Tone] + 0.5)], fs_ * 0.45), fs_, 0.70710678, 0);
}

void Processor::tap() {
    if (!prepared_) return;
    const double gap = clock_ - lastTap_;
    if (gap > 3.0 || tapCount_ == 0) { tapCount_ = 1; }
    else { for (int i = 3; i > 0; --i) tapGaps_[i] = tapGaps_[i - 1]; tapGaps_[0] = gap; ++tapCount_; const int k = std::min(tapCount_ - 1, 4); double s = 0; for (int i = 0; i < k; ++i) s += tapGaps_[i]; tapTime_ = std::clamp(s / k, 0.001, 2.0); write_ = true; }
    lastTap_ = clock_;
}
void Processor::midiClockTick() {
    if (!prepared_) return;
    if (tickCount_ == 0) tickClock_ = clock_;
    if (++tickCount_ >= 24) { const double dt = clock_ - tickClock_; if (dt > 0.1) midiBpm_ = 60.0 / dt; tickCount_ = 0; }
}
int Processor::takeParamWrite(int& id, double& plain) {
    if (!write_) return 0;
    write_ = false; id = Time; plain = std::clamp(tapTime_ * 1000.0, 1.0, 2000.0); return 7;
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    const int nc = std::min(numCh, 2);
    const double fb = target_[Feedback] * 0.01, a = 1.0 - std::exp(-1.0 / (0.1 * fs_)); const bool byp = target_[InputBypass] > 0.5;
    clock_ += static_cast<double>(n) / fs_;
    const double goal = goalS();
    for (int i = 0; i < n; ++i) {
        timeS_ += a * (goal - timeS_);
        const double d = timeS_ * fs_;
        for (int c = 0; c < nc; ++c) {
            auto& b = buf_[static_cast<size_t>(c)];
            const double rp = static_cast<double>(pos_) - d, fl = std::floor(rp), fr = rp - fl; const size_t i1 = static_cast<size_t>(static_cast<long long>(fl)) & mask_;
            const double y0 = b[(i1 + mask_) & mask_], y1 = b[i1], y2 = b[(i1 + 1) & mask_], y3 = b[(i1 + 2) & mask_];
            const double c0 = y1, c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
            const double e = ((c3 * fr + c2) * fr + c1) * fr + c0;
            const double in = byp ? 0.0 : ch[c][i];
            double rec = c_[static_cast<size_t>(c)].lp.process(c_[static_cast<size_t>(c)].hp.process(in + fb * e));
            rec = 1.6 * std::tanh(rec / 1.6);
            b[pos_ & mask_] = static_cast<float>(rec);
            const float o = static_cast<float>(e); ch[c][i] = std::abs(o) < 1e-30f ? 0.0f : o;
        }
        ++pos_;
    }
}

}  // namespace sw::lv25
