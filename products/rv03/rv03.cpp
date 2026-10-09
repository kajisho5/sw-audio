#include "rv03/rv03.hpp"
#include "sw/tail.hpp"
#include <algorithm>
#include <cmath>

namespace sw::rv03 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"rv03.springs", "Springs", 1, 3, 2,    Curve::Step, 1, {1, 2, 3}, "", {"1", "2", "3"}},
            {"rv03.dwell",   "Dwell",   0, 10, 5,   Curve::Lin, 1, {}, ""},
            {"rv03.tone",    "Tone",    0, 100, 50, Curve::Lin, 1, {}, "%"},
            {"rv03.tension", "Tension", 0, 10, 5,   Curve::Lin, 1, {}, ""},
            {"rv03.drip",    "Drip",    0, 10, 5,   Curve::Lin, 1, {}, ""},
            {"rv03.mix",     "Mix",     0, 100, 30, Curve::Lin, 1, {}, "%"},
            unitSpec("rv03.unit"),
        };
        v[Tone].minLabel = "Dark"; v[Tone].maxLabel = "Bright";
        return v;
    }();
    return s;
}
namespace {
constexpr double kA = -0.6;
constexpr double kLoopMs[3] = {33.0, 41.0, 52.0};
constexpr double kDecaySeconds = 2.5;
double stageDelay(double tension) { return 4.0 - 0.2 * tension; }   // M: 4 (loose, low chirp) .. 2 (tight, high chirp)
constexpr double kOutTrim = 5.0;   // calibrated: the impulse response energy is then about 1 at the defaults
}

double dispersionDelay(double f, double fs, double tension) {
    const double M = stageDelay(tension), w = 2.0 * 3.14159265358979323846 * f / fs;
    return 28.0 * M * (1.0 - kA * kA) / (1.0 + 2.0 * kA * std::cos(M * w) + kA * kA);
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::updateSprings() {
    const double fc = 1500.0 * std::pow(8000.0 / 1500.0, target_[Tone] * 0.01);
    toneC_ = 1.0 - std::exp(-2.0 * 3.14159265358979323846 * fc / fs_);
    for (int k = 0; k < 3; ++k) fb_[k] = std::pow(10.0, -3.0 * kLoopMs[k] * 0.001 / kDecaySeconds);
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (int k = 0; k < 3; ++k) {
        Spring& s = sp_[static_cast<size_t>(k)];
        s = Spring{};
        s.loopSamples = std::round(kLoopMs[k] * 0.001 * fs_ * 0.55);
        s.buf.assign(static_cast<size_t>(s.loopSamples) + 8, 0.0f);
    }
    fast_ = slow_ = gate_ = 0.0;
    updateSprings();
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (prepared_ && id == Tone) updateSprings();
}

void Processor::snapToTargets() { if (prepared_) updateSprings(); }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const int ns = static_cast<int>(target_[Springs] + 0.5);
    const double dwell = std::pow(10.0, (target_[Dwell] - 5.0) * 3.6 / 20.0), drip = target_[Drip] * 0.1;
    const double M = stageDelay(target_[Tension]); const int Mi = static_cast<int>(M); const double frac = M - Mi;
    const double fastR = std::exp(-1.0 / (0.01 * fs_)), slowA = 1.0 / (0.15 * fs_), gateR = std::exp(-1.0 / (0.06 * fs_));
    static const double panL[3][3] = {{1.0, 0.0, 0.0}, {1.0, 0.35, 0.0}, {1.0, 0.35, 0.7}}, panR[3][3] = {{1.0, 0.0, 0.0}, {0.35, 1.0, 0.0}, {0.35, 1.0, 0.7}};
    const double norm = kOutTrim / std::sqrt(static_cast<double>(ns));
    for (int i = 0; i < n; ++i) {
        const double x = nch > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i];
        const double ax = std::abs(x);
        fast_ = ax > fast_ ? ax : fast_ * fastR;
        slow_ += (ax - slow_) * slowA;
        gate_ = (fast_ > 1e-3 && fast_ > 2.0 * slow_) ? 1.0 : gate_ * gateR;
        const double exc = 0.5 + 1.0 * drip * gate_;
        const double in = std::tanh(dwell * x) * exc;
        double l = 0, r = 0;
        for (int k = 0; k < ns; ++k) {
            Spring& s = sp_[static_cast<size_t>(k)];
            const size_t sz = s.buf.size();
            const size_t rd = (s.pos + sz - static_cast<size_t>(s.loopSamples)) % sz;
            const double v = s.buf[rd];
            s.lp += toneC_ * (v - s.lp);
            double u = s.lp;
            const int w0 = s.idx & 7;
            for (int st = 0; st < kStages; ++st) {   // (a + z^-M) / (1 + a z^-M), M fractional by linear interpolation of the histories
                Stage& g = s.st[static_cast<size_t>(st)];
                const int i0 = (s.idx - Mi) & 7, i1 = (s.idx - Mi - 1) & 7;
                const double xm = g.xr[static_cast<size_t>(i0)] + frac * (g.xr[static_cast<size_t>(i1)] - g.xr[static_cast<size_t>(i0)]);
                const double ym = g.yr[static_cast<size_t>(i0)] + frac * (g.yr[static_cast<size_t>(i1)] - g.yr[static_cast<size_t>(i0)]);
                const double y = kA * u + xm - kA * ym;
                g.xr[static_cast<size_t>(w0)] = u; g.yr[static_cast<size_t>(w0)] = y;
                u = y;
            }
            s.idx = (s.idx + 1) & 7;
            s.buf[s.pos] = static_cast<float>(in + fb_[k] * u);
            s.pos = (s.pos + 1) % sz;
            const double o = u * std::sqrt(1.0 - fb_[k] * fb_[k]);
            l += panL[ns - 1][k] * o; r += panR[ns - 1][k] * o;
        }
        l *= norm; r *= norm;
        if (std::abs(l) < 1e-30) l = 0.0; if (std::abs(r) < 1e-30) r = 0.0;
        ch[0][i] = static_cast<float>(l);
        if (nch > 1) ch[1][i] = static_cast<float>(r);
    }
}

// the tail: the springs ring for kDecaySeconds (60 dB), taken down to -80 dB, and the dispersion adds a little
double Processor::tailSeconds() const { return tail::fromRt60(kDecaySeconds) + 0.4; }

}  // namespace sw::rv03
