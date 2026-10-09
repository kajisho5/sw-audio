#include "dl04/dl04.hpp"
#include "sw/tail.hpp"
#include "sw/notes.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace sw::dl04 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> v;
        for (int t = 1; t <= kTaps; ++t) for (const char* p : {"on", "time", "level", "pan", "filter"}) v.push_back("dl04.tap" + std::to_string(t) + "." + p);
        for (int t = 1; t <= kTaps; ++t) for (const char* p : {"On", "Time", "Level", "Pan", "Filter"}) v.push_back("Tap " + std::to_string(t) + " " + p);
        return v;
    }();
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v;
        static const double defMs[kTaps] = {250, 375, 500, 750, 1000, 1500};   // 1/8, 1/8 D, 1/4, 1/4 D, 1/2, 1/2 D at 120 bpm
        for (int t = 0; t < kTaps; ++t) {
            const size_t id = static_cast<size_t>(t * kPerTap), nm = static_cast<size_t>(kTaps * kPerTap + t * kPerTap);
            v.push_back({names[id + On].c_str(),     names[nm + On].c_str(),     0, 1, t < 3 ? 1.0 : 0.0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
            v.push_back({names[id + Time].c_str(),   names[nm + Time].c_str(),   1, 4000, defMs[t], Curve::Log, 1, {}, "ms"});
            v.push_back({names[id + Level].c_str(),  names[nm + Level].c_str(),  -60, 0, -6, Curve::Lin, 1, {}, "dB"});
            v.push_back({names[id + Pan].c_str(),    names[nm + Pan].c_str(),    -100, 100, (t & 1) ? 50.0 : -50.0, Curve::Lin, 1, {}, "%"});
            v.push_back({names[id + Filter].c_str(), names[nm + Filter].c_str(), 200, 20000, 8000, Curve::Log, 1, {}, "Hz"});
        }
        v.push_back({"dl04.feedback", "Feedback", 0, 100, 30, Curve::Lin, 1, {}, "%"});
        v.push_back({"dl04.mix",      "Mix",      0, 100, 20, Curve::Lin, 1, {}, "%"});
        v.push_back({"dl04.sync",     "Sync",     0, 1, 1,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
        v.push_back({"dl04.pingpong", "Ping-pong", 0, 1, 0,   Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
        return v;
    }();
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

double Processor::tapSeconds(int tap) const {
    double t = target_[static_cast<size_t>(tapParam(tap, Time))] * 0.001;
    if (target_[Sync] > 0.5 && bpm_ > 0.0) t = noteSeconds(noteNearest(t, 120.0), bpm_);   // the knob reads as ms at 120 bpm: the same note at every tempo
    return std::clamp(t, 0.001, kMaxSeconds);
}

void Processor::setFilters() {
    for (int t = 0; t < kTaps; ++t) {
        const double f = target_[static_cast<size_t>(tapParam(t, Filter))];
        for (int l = 0; l < 2; ++l) lp_[static_cast<size_t>(l)][static_cast<size_t>(t)].setup(Svf::Mode::LowPass, std::min(f, 0.45 * fs_), fs_, 0.7071, 0.0);
        filtHz_[static_cast<size_t>(t)] = f;
    }
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    size_t sz = 16; while (sz < static_cast<size_t>(kMaxSeconds * fs_) + 16) sz <<= 1;
    mask_ = sz - 1;
    for (auto& b : buf_) b.assign(sz, 0.0f);
    for (auto& l : lp_) for (auto& f : l) f.reset();
    pos_ = 0; cross_ = target_[PingPong] > 0.5 ? 1.0 : 0.0;
    snapToTargets();
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::snapToTargets() {
    for (int t = 0; t < kTaps; ++t) {
        const size_t k = static_cast<size_t>(t);
        delay_[k] = tapSeconds(t) * fs_;
        on_[k] = target_[static_cast<size_t>(tapParam(t, On))] > 0.5 ? 1.0 : 0.0;
        level_[k] = std::pow(10.0, target_[static_cast<size_t>(tapParam(t, Level))] / 20.0);
        pan_[k] = target_[static_cast<size_t>(tapParam(t, Pan))] * 0.01;
    }
    setFilters();
    cross_ = target_[PingPong] > 0.5 ? 1.0 : 0.0;
}

double Processor::read(int line, double d) const {
    const auto& b = buf_[static_cast<size_t>(line)];
    const double rp = static_cast<double>(pos_) - d;
    const double fl = std::floor(rp), f = rp - fl;
    const long i = static_cast<long>(fl);
    auto at = [&](long k) { return static_cast<double>(b[static_cast<size_t>(k) & mask_]); };
    const double y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const double c0 = y1, c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + c0;
}

// the repeats: the heard taps feed back together (the mean of them, Feedback each); with ping-pong a repeat crosses to the other line and back, two taps and two gains per trip
double Processor::tailSeconds() const {
    double d[kTaps]; int n = 0;
    for (int t = 0; t < kTaps; ++t) if (target_[static_cast<size_t>(tapParam(t, On))] > 0.5) d[n++] = tapSeconds(t);
    if (n == 0) return 0.3;
    const double fb = target_[Feedback] * 0.01;
    if (target_[PingPong] > 0.5) {
        double pair[kTaps * kTaps]; int m = 0;
        for (int a = 0; a < n; ++a) for (int b = 0; b < n; ++b) pair[m++] = d[a] + d[b];
        return tail::multi(pair, m, fb * fb / (static_cast<double>(n) * n)) + 0.3;
    }
    return tail::multi(d, n, fb / n) + 0.3;
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    for (int t = 0; t < kTaps; ++t) if (target_[static_cast<size_t>(tapParam(t, Filter))] != filtHz_[static_cast<size_t>(t)]) { setFilters(); break; }
    const double fb = target_[Feedback] * 0.01;
    const double glide = 1.0 - std::exp(-1.0 / (0.03 * fs_)), onStep = 1.0 / (0.01 * fs_), smooth = 1.0 - std::exp(-1.0 / (0.01 * fs_));
    double tgtDelay[kTaps], tgtLevel[kTaps], tgtPan[kTaps], tgtOn[kTaps];
    for (int t = 0; t < kTaps; ++t) {
        const size_t k = static_cast<size_t>(t);
        tgtDelay[k] = tapSeconds(t) * fs_;
        tgtOn[k] = target_[static_cast<size_t>(tapParam(t, On))] > 0.5 ? 1.0 : 0.0;
        tgtLevel[k] = std::pow(10.0, target_[static_cast<size_t>(tapParam(t, Level))] / 20.0);
        tgtPan[k] = target_[static_cast<size_t>(tapParam(t, Pan))] * 0.01;
    }
    const double crossTgt = target_[PingPong] > 0.5 ? 1.0 : 0.0;
    for (int i = 0; i < n; ++i) {
        cross_ += std::clamp(crossTgt - cross_, -0.002, 0.002);
        const double m = nch > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i];
        double outL = 0.0, outR = 0.0, sumAct = 0.0;
        std::array<double, 2> loopNew{0.0, 0.0};
        for (int t = 0; t < kTaps; ++t) {
            const size_t k = static_cast<size_t>(t);
            delay_[k] += glide * (tgtDelay[k] - delay_[k]);
            on_[k] += std::clamp(tgtOn[k] - on_[k], -onStep, onStep);
            level_[k] += smooth * (tgtLevel[k] - level_[k]);
            pan_[k] += smooth * (tgtPan[k] - pan_[k]);
            if (on_[k] <= 0.0) continue;
            const double d = std::max(3.0, delay_[k]);
            const double th = (pan_[k] + 1.0) * kPi / 4.0, gl = std::cos(th), gr = std::sin(th);
            sumAct += on_[k];
            for (int l = 0; l < 2; ++l) {
                if (l == 1 && cross_ <= 0.0) break;
                const double raw = read(l, d);
                const double y = (filtHz_[k] >= 19990.0 ? raw : lp_[static_cast<size_t>(l)][k].process(raw)) * on_[k];   // 20 kHz = open
                loopNew[static_cast<size_t>(l)] += y;
                const double lv = y * level_[k] * (l == 0 ? 1.0 : cross_);
                // line B has every pan mirrored
                if (l == 0) { outL += lv * gl; outR += lv * gr; } else { outL += lv * gr; outR += lv * gl; }
            }
        }
        const double norm = 1.0 / std::max(1.0, sumAct);
        // line A: input + its own loop (or B's while ping-pong); line B (only while ping-pong): A's loop
        const double l0 = loopNew[0] * norm, l1 = loopNew[1] * norm;
        buf_[0][pos_ & mask_] = static_cast<float>(m + fb * (l0 * (1.0 - cross_) + l1 * cross_));
        buf_[1][pos_ & mask_] = static_cast<float>(fb * l0 * cross_);
        ++pos_;
        if (std::abs(outL) < 1e-30) outL = 0.0;
        if (std::abs(outR) < 1e-30) outR = 0.0;
        ch[0][i] = static_cast<float>(outL);
        if (nch > 1) ch[1][i] = static_cast<float>(outR);
    }
}

}  // namespace sw::dl04
