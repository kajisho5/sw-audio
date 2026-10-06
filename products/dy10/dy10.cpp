#include "dy10/dy10.hpp"
#include <algorithm>
#include <cmath>

namespace sw::dy10 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v;
        static std::vector<std::string> names;   // ParamSpec keeps raw C strings
        names.clear(); names.reserve(64);
        auto keep = [&](const std::string& t) { names.push_back(t); return names.back().c_str(); };
        const char* kn[] = {"thresh", "ratio", "attack", "release", "range", "gain", "solo", "bypass"};
        const char* disp[] = {"Threshold", "Ratio", "Attack", "Release", "Range", "Gain", "Solo", "Bypass"};
        for (int n = 1; n <= 4; ++n)
            for (int k = 0; k < 8; ++k) {
                const std::string id = "dy10.b" + std::to_string(n) + "." + kn[k], nm = "Band " + std::to_string(n) + " " + disp[k];
                switch (k) {
                    case BThresh:  v.push_back({keep(id), keep(nm), -60, 0, 0,     Curve::Lin, 1, {}, "dB"}); break;
                    case BRatio:   v.push_back({keep(id), keep(nm), 1, 20, 2,      Curve::Log, 1, {}, ":1"}); break;
                    case BAttack:  v.push_back({keep(id), keep(nm), 0.1, 200, 15,  Curve::Skew, 3, {}, "ms"}); break;
                    case BRelease: v.push_back({keep(id), keep(nm), 5, 3000, 150,  Curve::Skew, 3, {}, "ms"}); break;
                    case BRange:   v.push_back({keep(id), keep(nm), -24, 0, -12,   Curve::Lin, 1, {}, "dB"}); break;
                    case BGain:    v.push_back({keep(id), keep(nm), -12, 12, 0,    Curve::Lin, 1, {}, "dB"}); break;
                    case BSolo:    v.push_back({keep(id), keep(nm), 0, 1, 0,       Curve::Step, 1, {0, 1}, "", {"Off", "On"}, nullptr, nullptr, 1.0, false}); break;
                    default:       v.push_back({keep(id), keep(nm), 0, 1, 0,       Curve::Step, 1, {0, 1}, "", {"Off", "On"}}); break;
                }
            }
        v.push_back({"dy10.x1.freq", "Crossover 1", 20, 20000, 240,  Curve::Log, 1, {}, "Hz"});
        v.push_back({"dy10.x2.freq", "Crossover 2", 20, 20000, 2000, Curve::Log, 1, {}, "Hz"});
        v.push_back({"dy10.x3.freq", "Crossover 3", 20, 20000, 8000, Curve::Log, 1, {}, "Hz"});
        v.push_back({"dy10.out", "Output", -24, 24, 0, Curve::Lin, 1, {}, "dB"});
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& ch : det_) for (auto& d : ch) { d.set(fs_, LevelDetector::Mode::Rms); d.reset(); }
    gr_ = {};
    for (auto& b : ball_) b.reset(0.0);
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id >= X1 && id <= X3) updateCrossovers();
    else if (id < 32) updateBand(id / 8);
}

void Processor::updateBand(int n) {
    const size_t b = static_cast<size_t>(n);
    comp_[b].set(target_[static_cast<size_t>(band(n, BThresh))], target_[static_cast<size_t>(band(n, BRatio))], 6.0);
    ball_[b].set(fs_, target_[static_cast<size_t>(band(n, BAttack))], target_[static_cast<size_t>(band(n, BRelease))]);
}

void Processor::updateCrossovers() {
    const double top = std::min(20000.0, fs_ * 0.45);
    double f1 = target_[X1], f2 = std::max(target_[X2], 2.0 * f1), f3 = std::max(target_[X3], 2.0 * f2);   // an octave apart, mover pushed back
    f3 = std::min(f3, top); f2 = std::min(f2, f3 / 2.0); f1 = std::min(f1, f2 / 2.0);
    eff_ = {f1, f2, f3};
    for (auto& x : xo_) {
        x.lp1.setup(Svf::Mode::LowPass, f1, fs_); x.hp1.setup(Svf::Mode::HighPass, f1, fs_);
        x.a2lo.setup(Svf::Mode::LowPass, f2, fs_); x.a2hi.setup(Svf::Mode::HighPass, f2, fs_);
        x.a3lo.setup(Svf::Mode::LowPass, f3, fs_); x.a3hi.setup(Svf::Mode::HighPass, f3, fs_);
        x.lp2.setup(Svf::Mode::LowPass, f2, fs_); x.hp2.setup(Svf::Mode::HighPass, f2, fs_);
        x.b3lo.setup(Svf::Mode::LowPass, f3, fs_); x.b3hi.setup(Svf::Mode::HighPass, f3, fs_);
        x.lp3.setup(Svf::Mode::LowPass, f3, fs_); x.hp3.setup(Svf::Mode::HighPass, f3, fs_);
    }
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    bool anySolo = false;
    for (int b = 0; b < 4; ++b) anySolo = anySolo || target_[static_cast<size_t>(band(b, BSolo))] > 0.5;
    const double out = std::pow(10.0, target_[Output] / 20.0);
    for (int i = 0; i < n; ++i) {
        double bands[2][4], lvl[4] = {-200, -200, -200, -200};
        for (int c = 0; c < nch; ++c) {
            Xover& x = xo_[static_cast<size_t>(c)];
            const double in = ch[c][i];
            const double rest = x.hp1.process(in);
            const double low = x.lp1.process(in);
            const double l2 = x.a2lo.process(low) + x.a2hi.process(low);
            bands[c][0] = x.a3lo.process(l2) + x.a3hi.process(l2);
            const double m = x.lp2.process(rest);
            bands[c][1] = x.b3lo.process(m) + x.b3hi.process(m);
            const double rest2 = x.hp2.process(rest);
            bands[c][2] = x.lp3.process(rest2);
            bands[c][3] = x.hp3.process(rest2);
            for (int b = 0; b < 4; ++b)
                lvl[b] = std::max(lvl[b], 20.0 * std::log10(std::max(det_[static_cast<size_t>(c)][static_cast<size_t>(b)].process(bands[c][b]), 1e-9)));
        }
        double g[4];
        for (int b = 0; b < 4; ++b) {
            const size_t bi = static_cast<size_t>(b);
            const bool bypass = target_[static_cast<size_t>(band(b, BBypass))] > 0.5;
            double gr = ball_[bi].process(std::max(comp_[bi].gainDb(lvl[b]), target_[static_cast<size_t>(band(b, BRange))]));
            gr_[bi] = bypass ? 0.0 : gr;
            const bool soloed = target_[static_cast<size_t>(band(b, BSolo))] > 0.5;
            g[b] = (anySolo && !soloed) ? 0.0 : (bypass ? 1.0 : std::pow(10.0, (gr + target_[static_cast<size_t>(band(b, BGain))]) / 20.0));
        }
        for (int c = 0; c < nch; ++c) {
            double y = (bands[c][0] * g[0] + bands[c][1] * g[1] + bands[c][2] * g[2] + bands[c][3] * g[3]) * out;
            if (!std::isfinite(y)) y = 0.0;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::dy10
