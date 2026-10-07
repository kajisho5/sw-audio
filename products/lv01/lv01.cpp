#include "lv01/lv01.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv01 {
namespace {
struct End { double depth, lowCut, mud, pres, air, thr, ratio; };
// end points at Voice 100 %  (noise depth dB, low cut Hz, mud dB, presence dB, air dB, comp threshold dBFS, comp ratio)
constexpr End kEnd[4] = {
    {18, 70, -2.5, 2.5, 2.0, -24, 3.0},   // Narration
    {14, 80, -3.0, 3.0, 1.5, -22, 4.0},   // Stream
    {20, 100, -2.0, 4.0, 0.0, -24, 3.5},  // Meeting
    {6, 40, -1.0, 2.0, 3.0, -20, 2.5},    // Singing
};
constexpr double kCeil = 0.89125093813374556;   // -1 dBFS
double dbToLin(double d) { return std::pow(10.0, d / 20.0); }
double coef(double fs, double ms) { return std::exp(-1.0 / (0.001 * ms * fs)); }
}  // namespace

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"lv01.use",   "Use",   0, 3, 1,   Curve::Step, 1, {0, 1, 2, 3}, "", {"Narration", "Stream", "Meeting", "Singing"}},
            {"lv01.voice", "Voice", 0, 100, 62, Curve::Lin, 1, {}, "%"},
            {"lv01.mute",  "Mute",  0, 1, 0,   Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        };
        v[Mute].automatable = false;
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate; split_.setup(250.0, 3000.0, fs_);
    for (auto& c : c_) { c = Chan{}; }
    cgain_ = lgain_ = 1; env_ = 0; warm_ = static_cast<int>(0.15 * fs_); prepared_ = true; wasBypass_ = true;
    updateStages(); mute_ = target_[Mute] > 0.5 ? 0.0 : 1.0;
}
void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v));
    if (prepared_ && id != Mute) updateStages();
}

void Processor::updateStages() {
    const End& e = kEnd[std::clamp(static_cast<int>(target_[Use]), 0, 3)]; const double v = target_[Voice] / 100.0;
    depth_ = e.depth * v; mud_ = e.mud * v; pres_ = e.pres * v; air_ = e.air * v;
    cutHz_ = 20.0 * std::pow(e.lowCut / 20.0, v);
    ratio_ = 1.0 + (e.ratio - 1.0) * v; thr_ = e.thr;
    makeup_ = std::max(0.0, -18.0 - e.thr) * (1.0 - 1.0 / ratio_) * 0.5;   // half of the reduction a -18 dBFS signal gets
    for (auto& c : c_) {
        c.hp.setup(Svf::Mode::HighPass, cutHz_, fs_, 0.70710678, 0);
        c.mud.setup(Svf::Mode::Bell, 250.0, fs_, 0.9, mud_);
        c.pres.setup(Svf::Mode::Bell, 3500.0, fs_, 0.9, pres_);
        c.air.setup(Svf::Mode::HighShelf, 10000.0, fs_, 0.70710678, air_);
    }
}

StageValues Processor::stage() const { return {depth_, cutHz_, mud_, pres_, air_, thr_, ratio_, makeup_, -1.0}; }
double Processor::noiseFloorDb(int b) const { return 20.0 * std::log10(std::max(1e-9, c_[0].floor[std::clamp(b, 0, 2)])); }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    const int nc = std::min(numCh, 2);
    const bool bypass = target_[Voice] <= 0.0;
    const double muteTarget = target_[Mute] > 0.5 ? 0.0 : 1.0, muteStep = 1.0 / (0.005 * fs_);
    if (bypass) {
        if (!wasBypass_) { for (auto& c : c_) { c.hp.reset(); c.mud.reset(); c.pres.reset(); c.air.reset(); for (int b = 0; b < 3; ++b) { c.env[b] = 0; c.gain[b] = 1; } } cgain_ = lgain_ = 1; env_ = 0; warm_ = static_cast<int>(0.15 * fs_); }
        wasBypass_ = true;
        if (mute_ == 1.0 && muteTarget == 1.0) return;
        for (int i = 0; i < n; ++i) {
            mute_ = mute_ < muteTarget ? std::min(muteTarget, mute_ + muteStep) : std::max(muteTarget, mute_ - muteStep);
            for (int c = 0; c < nc; ++c) { const float y = static_cast<float>(ch[c][i] * mute_); ch[c][i] = std::abs(y) < 1e-30f ? 0.0f : y; }
        }
        return;
    }
    wasBypass_ = false;
    const double aP = coef(fs_, 30.0), upRate = std::pow(10.0, 3.0 / 20.0 / fs_), floorCap = dbToLin(-42.0), floorMin = 1e-7;
    const double gOpen = coef(fs_, 2.0), gClose = coef(fs_, 80.0);
    const double cAtt = coef(fs_, 8.0), cRel = coef(fs_, 150.0), lRel = coef(fs_, 80.0);
    const double thrLin = dbToLin(thr_), mk = dbToLin(makeup_);
    for (int i = 0; i < n; ++i) {
        if (warm_ > 0) --warm_;
        double y[2] = {0, 0};
        for (int c = 0; c < nc; ++c) {
            Chan& ch_ = c_[static_cast<size_t>(c)];
            double bands[3]; split_.process(c, ch[c][i], bands);
            double s = 0;
            for (int b = 0; b < 3; ++b) {
                const double pw = bands[b] * bands[b];
                ch_.env[b] = aP * ch_.env[b] + (1 - aP) * pw;
                const double rms = std::sqrt(ch_.env[b]);
                if (warm_ > 0) ch_.floor[b] = std::min(std::max(floorMin, rms), floorCap); else if (rms < ch_.floor[b]) ch_.floor[b] = std::max(floorMin, rms); else ch_.floor[b] = std::min(std::max(ch_.floor[b] * upRate, floorMin), std::max(ch_.floor[b], floorCap));
                const double thrB = ch_.floor[b] * 2.0;   // floor + 6 dB
                double gdb = 0;
                if (rms < thrB) gdb = -std::min(depth_, 3.0 * 20.0 * std::log10(thrB / std::max(rms, 1e-12)));
                const double tg = dbToLin(gdb);
                const double k = tg > ch_.gain[b] ? gOpen : gClose;
                ch_.gain[b] = k * ch_.gain[b] + (1 - k) * tg;
                s += bands[b] * ch_.gain[b];
            }
            s = ch_.air.process(ch_.pres.process(ch_.mud.process(ch_.hp.process(s))));
            y[c] = s;
        }
        // compressor: linked peak detector, 8 ms attack / 150 ms release on the gain, soft knee 6 dB
        double pk = std::abs(y[0]); if (nc > 1) pk = std::max(pk, std::abs(y[1]));
        env_ = pk > env_ ? cAtt * env_ + (1 - cAtt) * pk : cRel * env_ + (1 - cRel) * pk;
        double gDb = 0;
        if (env_ > 1e-9) { const double lv = 20.0 * std::log10(env_), over = lv - thr_;
            if (over > 3.0) gDb = -over * (1.0 - 1.0 / ratio_); else if (over > -3.0) { const double t = over + 3.0; gDb = -(t * t / 12.0) * (1.0 - 1.0 / ratio_); } }
        const double tg = dbToLin(gDb); cgain_ = tg < cgain_ ? cAtt * cgain_ + (1 - cAtt) * tg : cRel * cgain_ + (1 - cRel) * tg;
        double o[2] = {y[0] * cgain_ * mk, nc > 1 ? y[1] * cgain_ * mk : 0.0};
        // limiter: instantaneous, 80 ms release; the sample peak never goes over -1 dBFS
        double lp = std::abs(o[0]); if (nc > 1) lp = std::max(lp, std::abs(o[1]));
        const double need = lp > kCeil ? kCeil / lp : 1.0;
        lgain_ = need < lgain_ ? need : lRel * lgain_ + (1 - lRel) * need;
        if (lgain_ * lp > kCeil) lgain_ = kCeil / lp;
        mute_ = mute_ < muteTarget ? std::min(muteTarget, mute_ + muteStep) : std::max(muteTarget, mute_ - muteStep);
        for (int c = 0; c < nc; ++c) { const float r = static_cast<float>(o[c] * lgain_ * mute_); ch[c][i] = std::abs(r) < 1e-30f ? 0.0f : r; }
    }
}

}  // namespace sw::lv01
