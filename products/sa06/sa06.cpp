#include "sa06/sa06.hpp"
#include <algorithm>
#include <cmath>

namespace sw::sa06 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v;
        static std::vector<std::string> names;
        names.clear(); names.reserve(64);
        auto keep = [&](const std::string& t) { names.push_back(t); return names.back().c_str(); };
        const char* kn[] = {"type", "drive", "shape", "bias", "dynamics", "mix"};
        const char* disp[] = {"Type", "Drive", "Shape", "Bias", "Dynamics", "Mix"};
        for (int n = 1; n <= 3; ++n)
            for (int k = 0; k < 6; ++k) {
                const std::string id = "sa06.b" + std::to_string(n) + "." + kn[k], nm = "Band " + std::to_string(n) + " " + disp[k];
                switch (k) {
                    case BType:  v.push_back({keep(id), keep(nm), 0, 4, 1, Curve::Step, 1, {0, 1, 2, 3, 4}, "", {"Tape", "Tube", "Diode", "Fold", "Fuzz"}}); break;
                    case BDrive: v.push_back({keep(id), keep(nm), 0, 24, 0, Curve::Lin, 1, {}, "dB"}); break;
                    case BShape: v.push_back({keep(id), keep(nm), 0, 2, 0, Curve::Step, 1, {0, 1, 2}, "", {"Soft", "Medium", "Hard"}}); break;
                    case BBias:  v.push_back({keep(id), keep(nm), -1, 1, 0, Curve::Lin, 1, {}, ""}); break;
                    case BDyn:   v.push_back({keep(id), keep(nm), -5, 5, 0, Curve::Lin, 1, {}, ""}); break;
                    default:     v.push_back({keep(id), keep(nm), 0, 100, 100, Curve::Lin, 1, {}, "%"}); break;
                }
            }
        v.push_back({"sa06.tone", "Tone", -6, 6, 0, Curve::Lin, 1, {}, "dB"});
        v.push_back({"sa06.output", "Output", -24, 24, 0, Curve::Lin, 1, {}, "dB"});
        return v;
    }();
    return s;
}

namespace {
double tapeShape(double u, double k) { return u / std::pow(1.0 + std::pow(std::abs(u), k), 1.0 / k); }
}  // namespace

// the five shapes; every one has slope 1 at 0 (the screen draws the same functions: ui/displays.js sa06Shape, checked against the same numbers)
double shapeFn(int type, int shape, double u) {
    switch (type) {
        case 0: { static const double k[3] = {2.0, 4.0, 8.0}, c[3] = {1.0, 0.7, 0.5}; return c[shape] * tapeShape(u / c[shape], k[shape]); }   // harder = lower ceiling and a sharper knee
        case 1: { static const double h[3] = {2.0, 1.4, 1.0}; const double b = 0.3, tb = std::tanh(b), s = 1.0 / (1.0 - tb * tb); return h[shape] * s * (std::tanh(u / h[shape] + b) - tb); }
        case 2: { static const double a[3] = {1.0, 3.0, 8.0}; return u >= 0 ? std::log1p(a[shape] * u) / a[shape] : -std::log1p(0.5 * a[shape] * -u) / (0.5 * a[shape]); }
        case 3: { static const double k[3] = {1.0, 2.0, 4.0}; return std::sin(k[shape] * u) / k[shape]; }
        default: { static const double k[3] = {2.0, 4.0, 12.0}; return u >= 0 ? tapeShape(u, k[shape]) : 0.6 * tapeShape(u / 0.6, k[shape]); }
    }
}
namespace {
constexpr double kPi = 3.14159265358979323846;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    split_.setup(200.0, std::min(3000.0, fs_ * 0.45), fs_);
    for (auto& b : bc_) for (auto& c : b) c = BandCh{};
    for (size_t b = 0; b < 3; ++b) drive_[b].reset(fs_, 20.0, target_[static_cast<size_t>(band(static_cast<int>(b), BDrive))]);
    dcA_ = std::exp(-2.0 * kPi * 5.0 / fs_);
    envC_ = std::exp(-1.0 / (0.010 * fs_));
    updateTone();
}

void Processor::updateTone() { for (auto& c : tone_) { c.lo.setup(Svf::Mode::LowShelf, 1000.0, fs_, 0.5, -target_[Tone]); c.hi.setup(Svf::Mode::HighShelf, 1000.0, fs_, 0.5, target_[Tone]); } }

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Tone) updateTone();
    else if (id < 18 && id % 6 == BDrive) drive_[static_cast<size_t>(id / 6)].setTarget(v);
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const bool tone = std::abs(target_[Tone]) > 1e-9;
    for (int i = 0; i < n; ++i) {
        double driveDb[3];
        for (int b = 0; b < 3; ++b) driveDb[b] = drive_[static_cast<size_t>(b)].next();
        for (int c = 0; c < nch; ++c) {
            double bands[3]; split_.process(c, ch[c][i], bands);
            double sum = 0;
            for (int b = 0; b < 3; ++b) {
                const int type = static_cast<int>(target_[static_cast<size_t>(band(b, BType))] + 0.5), shape = static_cast<int>(target_[static_cast<size_t>(band(b, BShape))] + 0.5);
                const double mix = target_[static_cast<size_t>(band(b, BMix))] / 100.0, bias = target_[static_cast<size_t>(band(b, BBias))], dyn = target_[static_cast<size_t>(band(b, BDyn))];
                BandCh& bc = bc_[static_cast<size_t>(b)][static_cast<size_t>(c)];
                const double xin = bands[b];
                bc.env = std::max(std::abs(xin), envC_ * bc.env);
                double dB = driveDb[b];
                if (dyn != 0.0) dB += dyn * 2.4 * std::clamp((20.0 * std::log10(std::max(bc.env, 1e-9)) + 18.0) / 18.0, -1.0, 1.0);
                if (dB < 0.01 && std::abs(bias) < 1e-9 && mix <= 0.0) { sum += xin; continue; }
                const double g = std::pow(10.0, std::clamp(dB, -12.0, 36.0) / 20.0), off = 0.5 * bias;
                // operating point: shape(u + off) - shape(off), divided by the slope there (small-signal gain 1)
                double slope = 1.0, f0 = 0.0;
                if (off != 0.0) { f0 = shapeFn(type, shape, off); slope = (shapeFn(type, shape, off + 1e-4) - shapeFn(type, shape, off - 1e-4)) / 2e-4; if (std::abs(slope) < 0.05) slope = 0.05; }
                auto f = [&](double u) { return (shapeFn(type, shape, u + off) - f0) / slope; };
                const double y = type >= 3 ? bc.x8.process(g * xin, f) : bc.x4.process(g * xin, f);
                bc.dc = dcA_ * bc.dc + (1.0 - dcA_) * (y - g * xin);   // the mean of what the shape added (bias, asymmetry)
                const double wet = y - bc.dc;
                sum += xin + mix * (wet - xin);
            }
            if (tone) sum = tone_[static_cast<size_t>(c)].hi.process(tone_[static_cast<size_t>(c)].lo.process(sum));
            if (!std::isfinite(sum)) sum = 0.0;
            if (std::abs(sum) < 1e-30) sum = 0.0;
            ch[c][i] = static_cast<float>(sum);
        }
    }
}

}  // namespace sw::sa06
