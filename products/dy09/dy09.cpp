#include "dy09/dy09.hpp"
#include <algorithm>
#include <cmath>

namespace sw::dy09 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"dy09.attack",     "Attack",  -15, 15, 0, Curve::Lin,  1, {}, "dB"},
        {"dy09.sustain",    "Sustain", -15, 15, 0, Curve::Lin,  1, {}, "dB"},
        {"dy09.speed",      "Speed",   0, 2, 1,    Curve::Step, 1, {0, 1, 2}, "", {"Fast", "Medium", "Slow"}},
        {"dy09.clip",       "Clip",    0, 2, 0,    Curve::Step, 1, {0, 1, 2}, "", {"Off", "Soft", "Hard"}},
        {"dy09.mix",        "Mix",     0, 100, 100, Curve::Lin, 1, {}, "%"},
        {"dy09.mode",       "Mode",    0, 1, 0,    Curve::Step, 1, {0, 1}, "", {"Smooth", "Split bands"}},
        {"dy09.b1.attack",  "Low Attack",   -15, 15, 0, Curve::Lin, 1, {}, "dB"},
        {"dy09.b1.sustain", "Low Sustain",  -15, 15, 0, Curve::Lin, 1, {}, "dB"},
        {"dy09.b2.attack",  "Mid Attack",   -15, 15, 0, Curve::Lin, 1, {}, "dB"},
        {"dy09.b2.sustain", "Mid Sustain",  -15, 15, 0, Curve::Lin, 1, {}, "dB"},
        {"dy09.b3.attack",  "High Attack",  -15, 15, 0, Curve::Lin, 1, {}, "dB"},
        {"dy09.b3.sustain", "High Sustain", -15, 15, 0, Curve::Lin, 1, {}, "dB"},
        oversampleSpec("dy09.os"),
    };
    return s;
}

namespace {
double coefMsFs(double fs, double ms) { return std::exp(-1.0 / (ms * 0.001 * fs)); }
constexpr double kRefDb = 12.0, kEps = 1e-6;   // ratio (dB) for the full amount; floor of the envelopes (-120 dBFS)
// fast / slow / slowest time constants in ms (design values); Medium is the default
constexpr double kTimes[3][3] = {{0.5, 5.0, 50.0}, {1.0, 15.0, 150.0}, {2.0, 40.0, 400.0}};
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& e : env_) e.reset();
    for (auto& c : os_) c.reset();
    cG_ = coefMsFs(fs_, 0.5);
    cPk_ = coefMsFs(fs_, 80.0);
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Speed) updateSpeed();
    if (id == Oversample) for (auto& o : os_) o.setFactor(static_cast<int>(v));
    if (id == Mode || id == Speed) {
        for (auto& s : split_) {
            s.lp150.setup(Svf::Mode::LowPass, 150.0, fs_); s.hp150.setup(Svf::Mode::HighPass, 150.0, fs_);
            const double f4 = std::min(4000.0, fs_ * 0.45);
            s.lpA.setup(Svf::Mode::LowPass, f4, fs_); s.hpA.setup(Svf::Mode::HighPass, f4, fs_);
            s.lpB.setup(Svf::Mode::LowPass, f4, fs_); s.hpB.setup(Svf::Mode::HighPass, f4, fs_);
        }
    }
}

void Processor::updateSpeed() {
    const int s = static_cast<int>(target_[Speed] + 0.5);
    cF_ = coefMsFs(fs_, kTimes[s][0]); cS_ = coefMsFs(fs_, kTimes[s][1]); cSS_ = coefMsFs(fs_, kTimes[s][2]);
}

double Processor::shapeDb(Env& e, double level, double att, double sus) const {
    e.fast = level + cF_ * (e.fast - level);
    e.slow = level + cS_ * (e.slow - level);
    e.slowest = level + cSS_ * (e.slowest - level);
    const double r1 = 20.0 * std::log10((e.fast + kEps) / (e.slow + kEps));      // > 0 on an onset
    const double r2 = 20.0 * std::log10((e.slow + kEps) / (e.slowest + kEps));   // < 0 while decaying
    const double a = att * std::clamp(r1 / kRefDb, 0.0, 1.0), s = sus * std::clamp(-r2 / kRefDb, 0.0, 1.0), want = a + s;
    e.aPk *= cPk_; if (std::abs(a) > std::abs(e.aPk)) e.aPk = a;      // held peaks of the two parts (screen only)
    e.sPk *= cPk_; if (std::abs(s) > std::abs(e.sPk)) e.sPk = s;
    e.gain = want + cG_ * (e.gain - want);
    return e.gain;
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const bool split = target_[Mode] > 0.5;
    const int clip = static_cast<int>(target_[Clip] + 0.5);
    for (int i = 0; i < n; ++i) {
        double x[2] = {0, 0}, y[2] = {0, 0};
        for (int c = 0; c < nch; ++c) x[c] = ch[c][i];
        if (!split) {
            double lvl = 0; for (int c = 0; c < nch; ++c) lvl += std::abs(x[c]); lvl /= nch;
            const double g = std::pow(10.0, shapeDb(env_[0], lvl, target_[Attack], target_[Sustain]) / 20.0);
            for (int c = 0; c < nch; ++c) y[c] = x[c] * g;
        } else {
            double band[2][3], lvl[3] = {0, 0, 0};
            for (int c = 0; c < nch; ++c) {
                Split& s = split_[static_cast<size_t>(c)];
                const double x1 = s.lp150.process(x[c]), x2 = s.hp150.process(x[c]);
                band[c][0] = s.lpA.process(x1) + s.hpA.process(x1);   // low band, all-passed to match the 4 kHz split
                band[c][1] = s.lpB.process(x2);
                band[c][2] = s.hpB.process(x2);
                for (int b = 0; b < 3; ++b) lvl[b] += std::abs(band[c][b]) / nch;
            }
            double g[3];
            for (int b = 0; b < 3; ++b)
                g[b] = std::pow(10.0, shapeDb(env_[static_cast<size_t>(b + 1)], lvl[b], target_[static_cast<size_t>(B1Attack + 2 * b)], target_[static_cast<size_t>(B1Sustain + 2 * b)]) / 20.0);
            for (int c = 0; c < nch; ++c) y[c] = band[c][0] * g[0] + band[c][1] * g[1] + band[c][2] * g[2];
        }
        for (int c = 0; c < nch; ++c) {
            double v = y[c];
            if (clip > 0) {
                v = os_[static_cast<size_t>(c)].process(v, [clip](double u) { return clip == 1 ? std::tanh(u) : std::clamp(u, -1.0, 1.0); });
                v = std::clamp(v, -1.0, 1.0);
            }
            if (!std::isfinite(v)) v = 0.0;
            if (std::abs(v) < 1e-30) v = 0.0;
            ch[c][i] = static_cast<float>(v);
        }
    }
}

}  // namespace sw::dy09
