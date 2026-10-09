#include "dl01/dl01.hpp"
#include "sw/notes.hpp"
#include <algorithm>
#include <cmath>

namespace sw::dl01 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"dl01.mode",     "Mode",     0, 2, 0,       Curve::Step, 1, {0, 1, 2}, "", {"Tape", "Analog", "Digital"}},
        {"dl01.time",     "Time",     1, 2000, 375,  Curve::Log, 1, {}, "ms"},
        {"dl01.feedback", "Feedback", 0, 110, 35,    Curve::Lin, 1, {}, "%"},
        {"dl01.hpf",      "HPF",      20, 1000, 100, Curve::Log, 1, {}, "Hz", {}, "Off"},
        {"dl01.lpf",      "LPF",      1000, 20000, 8000, Curve::Log, 1, {}, "Hz", {}, nullptr, "Off"},
        {"dl01.depth",    "Depth",    0, 100, 10,    Curve::Lin, 1, {}, "%"},
        {"dl01.rate",     "Rate",     0.1, 10, 0.5,  Curve::Log, 1, {}, "Hz"},
        {"dl01.duck",     "Duck",     0, 20, 0,      Curve::Lin, 1, {}, "dB"},
        {"dl01.mix",      "Mix",      0, 100, 25,    Curve::Lin, 1, {}, "%"},
        {"dl01.sync",     "Sync",     0, 1, 1,       Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"dl01.pingpong", "Ping-pong", 0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            unitSpec("dl01.unit"),
    };
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846;
double limitL(int mode) { return mode == Tape ? 1.0 : mode == Analog ? 1.6 : 8.0; }
double glideSeconds(int mode) { return mode == Tape ? 0.12 : mode == Analog ? 0.06 : 0.02; }
double devMs(int mode) { return mode == Tape ? 3.0 : mode == Analog ? 2.5 : 1.0; }
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

double Processor::timeSeconds() const {
    double t = target_[Time] * 0.001;
    if (target_[Sync] > 0.5 && bpm_ > 0.0) t = noteSeconds(noteIndexFromNorm(specs()[Time].toNorm(target_[Time])), bpm_);
    return std::clamp(t, 0.001, kMaxSeconds);
}

void Processor::setFilters() {
    const int mode = static_cast<int>(target_[Mode] + 0.5);
    for (auto& c : ch_) {
        c.hp.setup(Svf::Mode::HighPass, std::max(target_[Hpf], 20.0), fs_, 0.7071, 0.0);
        c.lp.setup(Svf::Mode::LowPass, std::min(target_[Lpf], 0.45 * fs_), fs_, 0.7071, 0.0);
        c.tone.setup(Svf::Mode::LowPass, mode == Tape ? 9000.0 : 5000.0, fs_, 0.7071, 0.0);
        c.tone2.setup(Svf::Mode::LowPass, 5000.0, fs_, 0.7071, 0.0);
    }
    filtMode_ = mode; filtHpf_ = target_[Hpf]; filtLpf_ = target_[Lpf];
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    size_t sz = 16; while (sz < static_cast<size_t>(kMaxSeconds * fs_) + 16) sz <<= 1;
    mask_ = sz - 1;
    for (auto& b : buf_) b.assign(sz, 0.0f);
    for (auto& c : ch_) { c.hp.reset(); c.lp.reset(); c.tone.reset(); c.tone2.reset(); }
    pos_ = 0; env_ = 0.0; duckGain_ = 1.0; ph_ = 0.0; phFl_ = 0.0; echo_ = {0.0, 0.0};
    delay_ = timeSeconds() * fs_;
    setFilters();
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::snapToTargets() { if (!prepared_) return; delay_ = timeSeconds() * fs_; setFilters(); }

double Processor::read(int c, double d) const {
    const auto& b = buf_[static_cast<size_t>(c)];
    const double rp = static_cast<double>(pos_) - d;
    const double fl = std::floor(rp); const double f = rp - fl;
    const long i = static_cast<long>(fl);
    auto at = [&](long k) { return static_cast<double>(b[static_cast<size_t>(k) & mask_]); };
    const double y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const double c0 = y1, c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + c0;
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const int mode = static_cast<int>(target_[Mode] + 0.5);
    if (mode != filtMode_ || target_[Hpf] != filtHpf_ || target_[Lpf] != filtLpf_) setFilters();
    const bool hpOn = target_[Hpf] > 20.0 * 1.0001, lpOn = target_[Lpf] < 20000.0 * 0.9999;
    const double fb = target_[Feedback] * 0.01, L = limitL(mode), invL = 1.0 / L;
    const double depth = target_[Depth] * 0.01, amp = depth * devMs(mode) * 0.001 * fs_;
    const double flAmp = mode == Tape ? depth * 0.05 * 0.001 * fs_ : 0.0;
    const double phInc = target_[Rate] / fs_, flInc = 6.3 / fs_;
    const double glide = 1.0 - std::exp(-1.0 / (glideSeconds(mode) * fs_));
    const double tgt = timeSeconds() * fs_;
    const bool ping = target_[PingPong] > 0.5 && nch > 1;
    const double duckDb = target_[Duck];
    const double envA = 1.0 - std::exp(-1.0 / (0.005 * fs_)), envR = 1.0 - std::exp(-1.0 / (0.15 * fs_));
    const double dgA = 1.0 - std::exp(-1.0 / (0.01 * fs_)), dgR = 1.0 - std::exp(-1.0 / (0.25 * fs_));
    for (int i = 0; i < n; ++i) {
        delay_ += glide * (tgt - delay_);
        ph_ += phInc; if (ph_ >= 1.0) ph_ -= 1.0;
        phFl_ += flInc; if (phFl_ >= 1.0) phFl_ -= 1.0;
        const double m = nch > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i];
        const double am = std::abs(m);
        env_ += (am > env_ ? envA : envR) * (am - env_);
        const double k = std::clamp((20.0 * std::log10(env_ + 1e-9) + 40.0) / 30.0, 0.0, 1.0);
        const double dgTarget = std::pow(10.0, -duckDb * k / 20.0);
        duckGain_ += (dgTarget < duckGain_ ? dgA : dgR) * (dgTarget - duckGain_);
        double in[2] = {ch[0][i], nch > 1 ? ch[1][i] : ch[0][i]};
        double rec[2];
        for (int c = 0; c < 2; ++c) {
            const double off = c == 0 ? 0.0 : 0.25;
            double ph = ph_ + off; if (ph >= 1.0) ph -= 1.0;
            double pf = phFl_ + off; if (pf >= 1.0) pf -= 1.0;
            const double d = std::max(3.0, delay_ + amp * std::sin(2.0 * kPi * ph) + flAmp * std::sin(2.0 * kPi * pf));
            echo_[static_cast<size_t>(c)] = read(c, d);
        }
        if (nch == 1) echo_[1] = echo_[0];
        for (int c = 0; c < 2; ++c) {
            double x;
            if (ping) x = (c == 0 ? m : 0.0) + fb * echo_[static_cast<size_t>(1 - c)];
            else x = in[c] + fb * echo_[static_cast<size_t>(c)];
            x = L * std::tanh(x * invL);
            auto& cc = ch_[static_cast<size_t>(c)];
            if (hpOn) x = cc.hp.process(x);
            if (lpOn) x = cc.lp.process(x);
            if (mode != Digital) x = cc.tone.process(x);
            if (mode == Analog) x = cc.tone2.process(x);
            rec[c] = x;
        }
        for (int c = 0; c < 2; ++c) buf_[static_cast<size_t>(c)][pos_ & mask_] = static_cast<float>(rec[c]);
        ++pos_;
        for (int c = 0; c < nch; ++c) {
            double y = echo_[static_cast<size_t>(c)] * duckGain_;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::dl01
