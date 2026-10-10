#include "vo05/vo05.hpp"
#include "sw/link_products.hpp"
#include <cstring>
#include <algorithm>
#include <cmath>
namespace sw::vo05 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"vo05.target",      "Target",      -40, -6, -18, Curve::Lin, 1, {}, "dB"},
        {"vo05.range",       "Range",       0, 12, 6,     Curve::Lin, 1, {}, "dB"},
        {"vo05.sensitivity", "Sensitivity", 0, 2, 1,      Curve::Step, 1, {0, 1, 2}, "", {"Low", "Mid", "High"}},
        {"vo05.breathskip",  "Breath skip", 0, 1, 1,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"vo05.ride",        "Ride",        -12, 12, 0,   Curve::Lin, 1, {}, "dB"},
        {"vo05.evo.on",      "Write automation", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}, nullptr, nullptr, 1.0, false},
    };
    static const std::vector<ParamSpec> all = [] {
        std::vector<ParamSpec> v = s;
        // Music from (appended at the end, SW Link; the spec: "the music comes in on the external sidechain or SW Link"): the sidechain, all the other SW AUDIO, or one product (not a vocal product: VO..)
        ParamSpec m{"vo05.link.music", "Music from", 0, 0, 0, Curve::Step, 1, {}, "", {}, nullptr, nullptr, 1.0, false};
        m.labels = {"Sidechain", "All other SW AUDIO"}; 
        size_t n = 0; const LinkProduct* lp = linkProducts(n);
        for (size_t k = 0; k < n; ++k) if (std::strncmp(lp[k].code, "VO", 2) != 0) m.labels.push_back(lp[k].label);
        for (size_t k = 0; k < m.labels.size(); ++k) m.steps.push_back(static_cast<double>(k));
        m.max = static_cast<double>(m.labels.size() - 1);
        v.push_back(m);
        return v;
    }();
    return all;
}
const char* Processor::musicProduct(int step) {
    if (step <= 0) return nullptr;
    if (step == 1) return "*";
    int k = step - 2; size_t n = 0; const LinkProduct* lp = linkProducts(n);
    for (size_t i = 0; i < n; ++i) if (std::strncmp(lp[i].code, "VO", 2) != 0) { if (k-- == 0) return lp[i].code; }
    return nullptr;
}
namespace {
constexpr double kTau[3] = {2.0, 0.8, 0.3}, kDead[3] = {1.5, 0.75, 0.25};   // Sensitivity Low / Mid / High (design values)
constexpr double kQuiet = -50.0, kBreathDb = 15.0, kPeakFall = 6.0;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& k : kv_) k.setup(fs_);
    for (auto& k : km_) k.setup(fs_);
    for (size_t c = 0; c < 2; ++c) for (size_t k = 0; k < 2; ++k) {
        hp_[c][k].setup(Svf::Mode::HighPass, 150.0, fs_, 0.70710678, 0);
        lp_[c][k].setup(Svf::Mode::LowPass, std::min(5000.0, fs_ * 0.45), fs_, 0.70710678, 0);
    }
    vocC_ = std::exp(-1.0 / (0.2 * fs_)); fastC_ = std::exp(-1.0 / (0.04 * fs_)); musC_ = std::exp(-1.0 / (3.0 * fs_));
    vocMs_ = vocRaw_ = musMs_ = 0; musAge_ = vocAge_ = 0; peakDb_ = -200; listening_ = false;
    rideDb_ = target_[Write] > 0.5 ? 0.0 : target_[Ride];
    gainFrom_ = gainTo_ = std::pow(10.0, rideDb_ / 20.0);
    open_ = false; dirty_ = false; moving_ = false; sent_ = rideDb_; ph_ = 0; vSum_ = rawSum_ = mSum_ = 0.0;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Ride && target_[Write] < 0.5) rideDb_ = v;   // host automation drives it while we are not writing
    else if (id == Write && v < 0.5) rideDb_ = target_[Ride];
}

double Processor::vocalLufs() const { return LoudnessMeter::lufs(vocMs_); }
double Processor::musicLufs() const { return LoudnessMeter::lufs(musMs_); }

int Processor::takeParamWrite(int& id, double& plain) {
    int f = 0;
    const bool writing = target_[Write] > 0.5;
    if (writing && !open_) { f |= 1; open_ = true; dirty_ = true; }
    if (open_ && (dirty_ || !writing)) { f |= 2; plain = rideDb_; id = Ride; sent_ = rideDb_; dirty_ = false; }
    if (!writing && open_) { f |= 4; open_ = false; }
    return f;
}

void Processor::processWithSidechain(float** ch, int numCh, int n, const float* const* sc, int scCh) {
    const bool fromLink = musicFromLink();
    const int nch = std::min(numCh, 2), nsc = (sc && !fromLink) ? std::min(scCh, 2) : 0;   // (music from SW Link: the sidechain is not listened to)
    scPresent_ = nsc > 0;
    // the control decisions are taken on a grid of the stream (every kControl samples, wherever the host's block starts), not per host block: the result does not depend on the block size.
    // The ride found at the end of a control block is ramped in over the next one.
    for (int start = 0; start < n;) {
        const int len = std::min(kControl - ph_, n - start);
        for (int i = start; i < start + len; ++i) {
            for (int c = 0; c < nch; ++c) {
                const double x = ch[c][i];
                double w = kv_[static_cast<size_t>(c)].process(x);
                auto& h = hp_[static_cast<size_t>(c)]; auto& l = lp_[static_cast<size_t>(c)];
                w = l[1].process(l[0].process(h[1].process(h[0].process(w))));
                vSum_ += w * w; rawSum_ += x * x;
            }
            for (int c = 0; c < nsc; ++c) { const double w = km_[static_cast<size_t>(c)].process(sc[c][i]); mSum_ += w * w; }
            const double g = gainFrom_ + (gainTo_ - gainFrom_) * (ph_ + (i - start) + 1) / kControl;
            for (int c = 0; c < nch; ++c) {
                double y = ch[c][i] * g;
                if (!std::isfinite(y)) y = 0.0;
                if (std::abs(y) < 1e-30) y = 0.0;
                ch[c][i] = static_cast<float>(y);
            }
        }
        ph_ += len; start += len;
        if (ph_ >= kControl) { ph_ = 0; control(nch); }
    }
}

void Processor::control(int nch) {
    const bool writing = target_[Write] > 0.5;
    const int sens = static_cast<int>(target_[Sensitivity] + 0.5);
    const double tau = kTau[sens], dead = kDead[sens], range = target_[Range];
    const int len = kControl;
    const double vSum = vSum_, rawSum = rawSum_, mSum = mSum_; vSum_ = rawSum_ = mSum_ = 0.0;
    const double af = std::pow(fastC_, len);
    vocRaw_ = af * vocRaw_ + (1.0 - af) * rawSum / (len * std::max(1, nch));   // fast level (40 ms): gate and breath decisions
    const double vocDb = vocRaw_ > 1e-20 ? 10.0 * std::log10(vocRaw_) : -200.0;
    peakDb_ = std::max(vocDb, peakDb_ - kPeakFall * len / fs_);
    const bool breath = target_[BreathSkip] > 0.5 && vocDb < peakDb_ - kBreathDb;
    const bool active = vocDb > kQuiet && !breath;
    if (active) {   // the vocal's level is the level while it is singing: pauses and breaths leave it where it was; the first 200 ms are a running mean
        vocAge_ += len;
        const double w = std::max(1.0 - std::pow(vocC_, len), static_cast<double>(len) / static_cast<double>(vocAge_));
        vocMs_ += w * (vSum / len - vocMs_);
    }
    const bool fromLink = musicFromLink(), haveMusic = fromLink ? linkValid_ : scPresent_;
    if (fromLink) {   // SW Link gave the level (short-term, already a 3 s window): it is the music level as it is
        if (linkValid_) { musAge_ += len; musMs_ = linkMs_; }
    } else if (scPresent_) {   // without a sidechain the music level is not updated (and not listening)
        musAge_ += len;
        const double w = std::max(1.0 - std::pow(musC_, len), static_cast<double>(len) / static_cast<double>(musAge_));   // the first 3 s are a running mean: no ramp up from silence
        musMs_ += w * (mSum / len - musMs_);
    }
    const double musDb = musMs_ > 1e-20 ? 10.0 * std::log10(musMs_) : -200.0;
    listening_ = haveMusic && musDb > kQuiet;
    if (writing) {
        if (listening_ && active && musAge_ >= static_cast<long>(fs_)) {   // the first second of music is only a rough estimate: no riding yet
            const double want = std::clamp(musicLufs() + target_[Target] - vocalLufs(), -range, range);
            const double err = want - rideDb_;
            if (std::abs(err) > dead) moving_ = true; else if (std::abs(err) < 0.03) moving_ = false;   // hysteresis: engage beyond the dead band, settle on the wanted value
            if (moving_) rideDb_ += err * (1.0 - std::exp(-len / (tau * fs_)));   // a ride set by hand / by the host is kept as it is until the controller moves it (Range limits the wanted value, not a value that was set)
        }
        if (std::abs(rideDb_ - sent_) >= 0.02) dirty_ = true;
    }
    gainFrom_ = gainTo_; gainTo_ = std::pow(10.0, rideDb_ / 20.0);
}

}  // namespace sw::vo05
