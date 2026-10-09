#include "vo07/vo07.hpp"
#include <algorithm>
#include <cmath>

namespace sw::vo07 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"vo07.hpf",      "HPF",      20, 300, 80,  Curve::Log, 1, {}, "Hz"},
        {"vo07.deess",    "De-ess",   0, 10, 0,     Curve::Lin, 1, {}, ""},
        {"vo07.breath",   "Breath",   0, 10, 0,     Curve::Lin, 1, {}, ""},
        {"vo07.body",     "Body",     -6, 6, 0,     Curve::Lin, 1, {}, "dB"},
        {"vo07.presence", "Presence", -6, 6, 0,     Curve::Lin, 1, {}, "dB"},
        {"vo07.air",      "Air",      -6, 6, 0,     Curve::Lin, 1, {}, "dB"},
        {"vo07.comp",     "Comp",     0, 10, 0,     Curve::Lin, 1, {}, ""},
        {"vo07.level",    "Level",    -12, 12, 0,   Curve::Lin, 1, {}, "dB"},
        {"vo07.plate",    "Plate",    0, 10, 0,     Curve::Lin, 1, {}, ""},
        {"vo07.echo",     "Echo",     0, 10, 0,     Curve::Lin, 1, {}, ""},
        {"vo07.out",      "Output",   -10, 10, 0,   Curve::Lin, 1, {}, "dB"},
            unitSpec("vo07.unit"),
    };
    return s;
}

namespace {
constexpr double kAirCornerHz = 6500.0, kDeessDbPerUnit = 1.5, kBreathDbPerUnit = 1.5, kSend = 0.5, kQuietDb = 15.0, kPeakFall = 6.0, kZcr = 0.12, kEchoDefaultMs = 250.0;
double db2g(double db) { return std::pow(10.0, db / 20.0); }
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::setTone() {
    for (auto& c : ch_) {
        c.hp.setup(Svf::Mode::HighPass, std::min(target_[Hpf], 0.45 * fs_), fs_, 0.70710678, 0.0);
        c.body.setup(Svf::Mode::Bell, 200.0, fs_, 0.8, target_[Body]);
        c.pres.setup(Svf::Mode::Bell, 3000.0, fs_, 0.9, target_[Presence]);
        c.air.setup(Svf::Mode::HighShelf, std::min(kAirCornerHz, 0.45 * fs_), fs_, 0.70710678, target_[Air]);
    }
    deess_.setParam(dy05::Range, -kDeessDbPerUnit * target_[Deess]);
    comp_.setParam(dy02::Level, target_[Comp]);
}

void Processor::setEcho() {
    const double ms = bpm_ > 0.0 ? 30000.0 / bpm_ : kEchoDefaultMs;
    echo_.setParam(dl01::Time, std::clamp(ms, 1.0, 2000.0));
}

void Processor::prepare(double sampleRate, int maxBlock) {
    fs_ = sampleRate; maxBlock_ = std::max(1, maxBlock);
    deess_.setParam(dy05::Mode, 1); deess_.setParam(dy05::Freq, 6500); deess_.setParam(dy05::Threshold, -30); deess_.setParam(dy05::Lookahead, 0); deess_.setParam(dy05::Listen, 0); deess_.setParam(dy05::Pitch, 1);
    comp_.setParam(dy02::Output, 0); comp_.setParam(dy02::Speed, 1); comp_.setParam(dy02::Emphasis, 0); comp_.setParam(dy02::Mix, 100); comp_.setParam(dy02::Ride, 0); comp_.setParam(dy02::AutoMakeup, 1);
    plate_.setParam(rv02::Decay, 1.8); plate_.setParam(rv02::PreDelay, 20); plate_.setParam(rv02::Damping, 50); plate_.setParam(rv02::LowCut, 120); plate_.setParam(rv02::Width, 100);
    plate_.setParam(rv02::Mix, 100); plate_.setParam(rv02::MonoIn, 0); plate_.setParam(rv02::Sync, 0); plate_.setParam(rv02::DuckOn, 0);
    echo_.setParam(dl01::Mode, 1); echo_.setParam(dl01::Feedback, 30); echo_.setParam(dl01::Hpf, 200); echo_.setParam(dl01::Lpf, 6000); echo_.setParam(dl01::Depth, 10); echo_.setParam(dl01::Rate, 0.5);
    echo_.setParam(dl01::Duck, 0); echo_.setParam(dl01::Mix, 100); echo_.setParam(dl01::Sync, 0); echo_.setParam(dl01::PingPong, 0);
    setEcho(); setTone();
    deess_.prepare(fs_, maxBlock_); comp_.prepare(fs_, maxBlock_); plate_.prepare(fs_, maxBlock_); echo_.prepare(fs_, maxBlock_);
    for (auto& b : sendP_) b.assign(static_cast<size_t>(maxBlock_), 0.0f);
    for (auto& b : sendE_) b.assign(static_cast<size_t>(maxBlock_), 0.0f);
    for (auto& c : ch_) { c.hp.reset(); c.body.reset(); c.pres.reset(); c.air.reset(); }
    env_ = 0.0; peakDb_ = -200.0; breathDb_ = 0.0; zcr_ = 0.0; prevX_ = 0.0;
    deess_.snapToTargets(); comp_.snapToTargets(); plate_.snapToTargets(); echo_.snapToTargets();
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (prepared_ && id <= Comp) setTone();
}

void Processor::setTempo(double bpm) { bpm_ = bpm; plate_.setTempo(bpm); echo_.setTempo(bpm); if (prepared_) setEcho(); }

void Processor::snapToTargets() { if (prepared_) { setTone(); setEcho(); deess_.snapToTargets(); comp_.snapToTargets(); plate_.snapToTargets(); echo_.snapToTargets(); } }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    for (int off = 0; off < n; off += maxBlock_) {
        float* p[2] = {ch[0] + off, ch[std::min(numCh, 2) - 1] + off};
        chunk(p, std::min(numCh, 2), std::min(maxBlock_, n - off));
    }
}

void Processor::chunk(float** ch, int nch, int n) {
    // 1. clean: HPF
    for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) ch[c][i] = static_cast<float>(ch_[static_cast<size_t>(c)].hp.process(ch[c][i]));
    // 2. De-ess (DY05 core)
    deess_.process(ch, nch, n);
    // 3. Breath
    const double rel = std::exp(-1.0 / (0.04 * fs_)), zk = std::exp(-1.0 / (0.02 * fs_));
    const double aA = 1.0 - std::exp(-1.0 / (0.01 * fs_)), aR = 1.0 - std::exp(-1.0 / (0.06 * fs_));
    const double amount = kBreathDbPerUnit * target_[Breath];
    for (int i = 0; i < n; ++i) {
        const double x = nch > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i];
        env_ = rel * env_ + (1.0 - rel) * x * x;
        zcr_ = zk * zcr_ + (1.0 - zk) * (((x >= 0.0) != (prevX_ >= 0.0) && std::abs(x) > 1e-6) ? 1.0 : 0.0); prevX_ = x;
        const double db = env_ > 1e-14 ? 10.0 * std::log10(env_) : -140.0;
        peakDb_ = std::max(db, peakDb_ - kPeakFall / fs_);
        const bool breath = amount > 0.0 && db < peakDb_ - kQuietDb && db > -90.0 && zcr_ > kZcr;
        const double want = breath ? -amount : 0.0;
        breathDb_ += (want - breathDb_) * (want < breathDb_ ? aA : aR);
        if (amount > 0.0 || std::abs(breathDb_) > 1e-4) { const float g = static_cast<float>(db2g(breathDb_)); for (int c = 0; c < nch; ++c) ch[c][i] *= g; }
    }
    // 4. tone (a bell at 0 dB is flat: only the shelves / bells that are set run)
    for (int c = 0; c < nch; ++c) {
        auto& cc = ch_[static_cast<size_t>(c)];
        for (int i = 0; i < n; ++i) {
            double y = ch[c][i];
            if (target_[Body] != 0.0) y = cc.body.process(y);
            if (target_[Presence] != 0.0) y = cc.pres.process(y);
            if (target_[Air] != 0.0) y = cc.air.process(y);
            ch[c][i] = static_cast<float>(y);
        }
    }
    // 5. comp (DY02 core, Auto makeup) and Level
    comp_.process(ch, nch, n);
    const double lg = db2g(target_[Level]);
    if (lg != 1.0) for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) ch[c][i] = static_cast<float>(ch[c][i] * lg);
    // 6. sends
    const double pg = kSend * target_[Plate] * 0.1, eg = kSend * target_[Echo] * 0.1;
    float* sp[2] = {sendP_[0].data(), sendP_[nch > 1 ? 1 : 0].data()};
    float* se[2] = {sendE_[0].data(), sendE_[nch > 1 ? 1 : 0].data()};
    if (pg > 0.0) { for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) sp[c][i] = ch[c][i]; plate_.process(sp, nch, n); }
    if (eg > 0.0) { for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) se[c][i] = ch[c][i]; echo_.process(se, nch, n); }
    for (int c = 0; c < nch; ++c)
        for (int i = 0; i < n; ++i) {
            double y = ch[c][i];
            if (pg > 0.0) y += pg * sp[c][i];
            if (eg > 0.0) y += eg * se[c][i];
            if (!std::isfinite(y)) y = 0.0;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
}

}  // namespace sw::vo07
