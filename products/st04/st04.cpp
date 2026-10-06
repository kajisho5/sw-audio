#include "st04/st04.hpp"
#include <algorithm>
#include <cmath>

namespace sw::st04 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"st04.center",    "Center",     0, 100, 50, Curve::Lin, 1, {}, "%", {}, "Wide", "Focus", 0.98},
        {"st04.haas",      "Haas",       0, 40, 0,   Curve::Skew, 2, {}, "ms"},
        {"st04.side",      "Side",       0, 1, 0,    Curve::Step, 1, {0, 1}, "", {"L", "R"}},
        {"st04.lowcenter", "Low center", 0, 10, 0,   Curve::Lin, 1, {}, "", {}, "Off"},
        {"st04.balance",   "Balance",    -100, 100, 0, Curve::Lin, 1, {}, "%"},
        {"st04.link",      "Link",       0, 1, 1,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"st04.evo.on",    "Mono safe",  0, 1, 0,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}
double sideGain(double c) {
    c = std::clamp(c, 0.0, 100.0);
    if (c <= 50.0) return std::pow(10.0, 6.0 * (1.0 - c / 50.0) / 20.0);
    return std::max(0.0, 1.0 - (c - 50.0) / 50.0);
}
double lowCenterHz(double v) { return v <= 0.0 ? 0.0 : 20.0 * std::pow(15.0, std::min(v, 10.0) / 10.0); }

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

namespace {
constexpr double kRMax = 0.52;
}

double Processor::delayedSideGainDb() const {
    const double d = target_[Haas];
    if (d <= 0.0) return 0.0;
    const bool delayedLeft = target_[Side] < 0.5;
    const double b = target_[Balance] * 0.01, gl = b > 0 ? 1.0 - b : 1.0, gr = b < 0 ? 1.0 + b : 1.0;
    double g = (target_[Link] > 0.5 ? std::min(kLinkMaxDb, kLinkDbPerMs * d) : 0.0);
    double gd = std::pow(10.0, g / 20.0) * (delayedLeft ? gl : gr);
    const double go = delayedLeft ? gr : gl;
    if (target_[MonoSafe] > 0.5 && go > 0.0 && gd / go > kRMax) gd = kRMax * go;
    return 20.0 * std::log10(std::max(gd / std::max(go, 1e-9), 1e-9));
}
double Processor::monoCombDepthDb() const {
    if (target_[Haas] <= 0.0) return 0.0;
    const double r = std::pow(10.0, delayedSideGainDb() / 20.0);
    const double rr = std::min(r, 1.0 / std::max(r, 1e-9));   // the weaker over the stronger: the notch depends on the ratio, either way round
    return 20.0 * std::log10(std::max((1.0 - rr) / (1.0 + rr), 1e-6));
}

void Processor::update() {
    const double f = lowCenterHz(target_[LowCenter]);
    if (f != hpHz_) { if (f > 0.0) { hp1_.setup(Svf::Mode::HighPass, f, fs_, 0.5412, 0.0); hp2_.setup(Svf::Mode::HighPass, f, fs_, 1.3065, 0.0); } hpHz_ = f; }
    haasT_ = target_[Haas] * 0.001 * fs_;
    const bool delayedLeft = target_[Side] < 0.5;
    const double b = target_[Balance] * 0.01, bl = b > 0 ? 1.0 - b : 1.0, br = b < 0 ? 1.0 + b : 1.0;
    double gl = bl, gr = br, lp = 20000.0;
    if (target_[Haas] > 0.0) {
        const double link = target_[Link] > 0.5 ? std::pow(10.0, std::min(kLinkMaxDb, kLinkDbPerMs * target_[Haas]) / 20.0) : 1.0;
        double& gd = delayedLeft ? gl : gr; const double go = delayedLeft ? gr : gl;
        gd *= link;
        if (target_[MonoSafe] > 0.5 && go > 0.0 && gd / go > kRMax) { const double cut = kRMax / (gd / go); gd = kRMax * go; lp = std::clamp(20000.0 * cut * cut, 3000.0, 20000.0); }
    }
    gLt_ = gl; gRt_ = gr;
    if (lp != lpHz_) { lp_.setup(Svf::Mode::LowPass, std::min(lp, 0.45 * fs_), fs_, 0.7071, 0.0); lpHz_ = lp; }
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    size_t sz = 16; while (sz < static_cast<size_t>(0.05 * fs_) + 16) sz <<= 1;
    mask_ = sz - 1;
    for (auto& b : buf_) b.assign(sz, 0.0f);
    hp1_.reset(); hp2_.reset(); lp_.reset();
    pos_ = 0; hpHz_ = -1.0; lpHz_ = -1.0;
    update(); haasD_ = haasT_; gL_ = gLt_; gR_ = gRt_;
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::snapToTargets() { if (prepared_) { update(); haasD_ = haasT_; gL_ = gLt_; gR_ = gRt_; } }

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
    if (!prepared_ || numCh < 2) return;
    update();
    const double sg = sideGain(target_[Center]);
    const bool hpOn = hpHz_ > 0.0, delayedLeft = target_[Side] < 0.5, lpOn = lpHz_ < 19999.0;
    const double k = 1.0 - std::exp(-1.0 / (0.02 * fs_)), glide = 1.0 - std::exp(-1.0 / (0.03 * fs_));
    for (int i = 0; i < n; ++i) {
        gL_ += k * (gLt_ - gL_); gR_ += k * (gRt_ - gR_); haasD_ += glide * (haasT_ - haasD_);
        const double m = 0.5 * (ch[0][i] + ch[1][i]);
        double s = 0.5 * (ch[0][i] - ch[1][i]) * sg;
        if (hpOn) s = hp2_.process(hp1_.process(s));
        double l = m + s, r = m - s;
        buf_[0][pos_ & mask_] = static_cast<float>(l); buf_[1][pos_ & mask_] = static_cast<float>(r);
        ++pos_;
        if (haasD_ > 0.5) {   // a delay of at least half a sample (the newest sample sits at delay 1)
            const double d = std::max(3.0, haasD_ + 1.0);
            if (delayedLeft) { l = read(0, d); if (lpOn) l = lp_.process(l); } else { r = read(1, d); if (lpOn) r = lp_.process(r); }
        }
        l *= gL_; r *= gR_;
        if (std::abs(l) < 1e-30) l = 0.0;
        if (std::abs(r) < 1e-30) r = 0.0;
        ch[0][i] = static_cast<float>(l); ch[1][i] = static_cast<float>(r);
    }
}

}  // namespace sw::st04
