#include "md07/md07.hpp"
#include <algorithm>
#include <cmath>

namespace sw::md07 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"md07.voices", "Voices", 2, 6, 4,   Curve::Step, 1, {2, 3, 4, 6}, "", {"2", "3", "4", "6"}},
        {"md07.spread", "Spread", 0, 10, 6,  Curve::Lin, 1, {}, ""},
        {"md07.rate",   "Rate",   0, 10, 4,  Curve::Lin, 1, {}, ""},
        {"md07.depth",  "Depth",  0, 10, 5,  Curve::Lin, 1, {}, ""},
        {"md07.tone",   "Tone",   0, 100, 50, Curve::Lin, 1, {}, "%"},
        {"md07.mix",    "Mix",    0, 100, 50, Curve::Lin, 1, {}, "%"},
    };
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846, kCentreMs = 10.0, kSlowDevMs = 4.0, kFastRatio = 12.0, kFastDevShare = 0.25;
}
double slowHz(double rate) { return 0.15 * std::pow(20.0, rate / 10.0); }
double voicePan(int v, int n, double spread) { return n < 2 ? 0.5 : 0.5 + std::clamp(spread * 0.1, 0.0, 1.0) * (static_cast<double>(v) / (n - 1) - 0.5); }

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::setTone() {
    const double cut = 2500.0 * std::pow(14000.0 / 2500.0, target_[Tone] * 0.01);
    for (auto& c : ch_) { c.lp.setup(Svf::Mode::LowPass, std::min(cut, 0.45 * fs_), fs_, 0.7071, 0.0); c.hp.setup(Svf::Mode::HighPass, 120.0, fs_, 0.7071, 0.0); }
    toneState_ = target_[Tone];
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    size_t sz = 16; while (sz < static_cast<size_t>(0.05 * fs_) + 16) sz <<= 1;
    mask_ = sz - 1;
    for (auto& b : buf_) b.assign(sz, 0.0f);
    for (auto& c : ch_) { c.lp.reset(); c.hp.reset(); }
    pos_ = 0; slowPh_ = fastPh_ = 0.0;
    setTone();
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::snapToTargets() { if (prepared_) setTone(); }

double Processor::read(int c, double d) const {
    const auto& b = buf_[static_cast<size_t>(c)];
    const double rp = static_cast<double>(pos_) - d;
    const double fl = std::floor(rp), f = rp - fl;
    const long i = static_cast<long>(fl);
    auto at = [&](long k) { return static_cast<double>(b[static_cast<size_t>(k) & mask_]); };
    const double y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const double c0 = y1, c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + c0;
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    if (target_[Tone] != toneState_) setTone();
    const int voices = std::clamp(static_cast<int>(target_[Voices] + 0.5), 2, kMaxVoices);
    const double depth = target_[Depth] * 0.1, slowDev = depth * kSlowDevMs * 0.001 * fs_, fastDev = slowDev * kFastDevShare, centre = kCentreMs * 0.001 * fs_;
    const double incSlow = slowHz(target_[Rate]) / fs_, incFast = kFastRatio * slowHz(target_[Rate]) / fs_;
    double gl[kMaxVoices], gr[kMaxVoices], norm[2] = {0.0, 0.0};
    for (int v = 0; v < voices; ++v) { const double p = voicePan(v, voices, target_[Spread]); gl[v] = 1.0 - p; gr[v] = p; norm[0] += gl[v] * gl[v]; norm[1] += gr[v] * gr[v]; }
    const double nrm = 1.0 / std::sqrt(0.5 * (norm[0] + norm[1]));   // both channels share the one factor (the pan balance stays); unit power on average
    for (int i = 0; i < n; ++i) {
        slowPh_ += incSlow; if (slowPh_ >= 1.0) slowPh_ -= 1.0;
        fastPh_ += incFast; if (fastPh_ >= 1.0) fastPh_ -= 1.0;
        for (int c = 0; c < 2; ++c) buf_[static_cast<size_t>(c)][pos_ & mask_] = static_cast<float>(c < nch ? ch[c][i] : ch[0][i]);
        ++pos_;
        double out[2] = {0.0, 0.0};
        for (int v = 0; v < voices; ++v) {
            double ps = slowPh_ + static_cast<double>(v) / voices, pf = fastPh_ - static_cast<double>(v) / voices;
            ps -= std::floor(ps); pf -= std::floor(pf);
            const double d = centre + slowDev * std::sin(2.0 * kPi * ps) + fastDev * std::sin(2.0 * kPi * pf);
            lastDelay_[v] = d;
            // the left and the right line carry the same signal when the input is mono; a stereo input keeps its channels in their own lines
            out[0] += gl[v] * read(0, d); out[1] += gr[v] * read(1, d);
        }
        for (int c = 0; c < nch; ++c) {
            auto& cc = ch_[static_cast<size_t>(c)];
            double y = cc.hp.process(cc.lp.process(out[c] * nrm));
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::md07
