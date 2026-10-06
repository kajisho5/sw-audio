// SW AUDIO core — common processing frame around a product core (spec 共通章 4「信号の流れ」, 5「共通機能」)
//   input -> [dry tap, delayed by the core latency] -> core -> Auto gain -> Mix -> Output -> (Δ) -> In crossfade
// Core interface: prepare(fs, maxBlock), process(float** ch, int numCh, int n) in place, latencySamples().
#pragma once
#include "sw/loudness.hpp"
#include "sw/smooth.hpp"
#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>
#include <vector>

namespace sw {

// cores that can use an external sidechain implement processWithSidechain(ch, nch, n, sc, scCh)
template <class C, class = void> struct AcceptsSidechain : std::false_type {};
template <class C>
struct AcceptsSidechain<C, std::void_t<decltype(std::declval<C&>().processWithSidechain(
                              std::declval<float**>(), 0, 0, std::declval<const float* const*>(), 0))>> : std::true_type {};

template <class Core>
class Shell {
public:
    static constexpr double kAutoGainLimitDb = 18.0;    // spec: 補正幅 ±18 dB
    static constexpr double kAutoGainTimeS = 2.0;       // spec: 追従 2 秒
    static constexpr double kAutoGainGateLufs = -60.0;  // spec: −60 LUFS 以下の無音では更新しない

    void prepare(double fs, int maxBlock, int numCh) {
        fs_ = fs;
        nch_ = std::clamp(numCh, 1, 2);
        core_.prepare(fs, maxBlock);
        lat_ = std::max(0, core_.latencySamples());
        delay_.assign(static_cast<size_t>(nch_), std::vector<float>(static_cast<size_t>(std::max(1, lat_)), 0.0f));
        dpos_ = 0;
        dry_.assign(static_cast<size_t>(nch_), std::vector<float>(static_cast<size_t>(std::max(1, maxBlock)), 0.0f));
        dryMeter_.setup(fs, nch_);
        wetMeter_.setup(fs, nch_);
        block_ = std::max(1, static_cast<int>(std::lround(fs * 0.1)));
        untilBlock_ = block_;
        agcDb_ = 0.0;
        out_.reset(fs, 20.0, out_.target() == 0.0 ? 1.0 : out_.target());
        agc_.reset(fs, 100.0, 1.0);
        inMix_.reset(fs, 10.0, in_ ? 1.0 : 0.0);
        deltaMix_.reset(fs, 10.0, delta_ ? 1.0 : 0.0);
        mix_.reset(fs, 20.0, mix_.target());
    }

    void setIn(bool on) { in_ = on; inMix_.setTarget(on ? 1.0 : 0.0); }
    void setDelta(bool on) { delta_ = on; deltaMix_.setTarget(on ? 1.0 : 0.0); }
    void setAutoGain(bool on) { autoGain_ = on; agc_.setTarget(on ? dbToGain(agcDb_) : 1.0); }
    void setOutputDb(double db) { out_.setTarget(dbToGain(db)); }
    void setMix(double fraction) { mix_.setTarget(std::clamp(fraction, 0.0, 1.0)); }  // product Mix (spec step 4)
    void snap() { for (LinearSmoother* s : {&out_, &agc_, &inMix_, &deltaMix_, &mix_}) s->skip(1 << 30); }

    Core& core() { return core_; }
    const Core& core() const { return core_; }
    int latencySamples() const { return lat_; }
    double autoGainDb() const { return agcDb_; }  // current correction estimate (display)

    // sc / scCh: optional external sidechain (nullptr / 0 when the host provides none)
    void process(float** ch, int numCh, int n, const float* const* sc = nullptr, int scCh = 0) {
        const int nch = std::min(numCh, nch_);
        // 1. latency-aligned dry copy
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < nch; ++c) {
                const float x = ch[c][i];
                if (lat_ > 0) {
                    auto& d = delay_[static_cast<size_t>(c)];
                    dry_[static_cast<size_t>(c)][static_cast<size_t>(i)] = d[static_cast<size_t>(dpos_)];
                    d[static_cast<size_t>(dpos_)] = x;
                } else {
                    dry_[static_cast<size_t>(c)][static_cast<size_t>(i)] = x;
                }
            }
            if (lat_ > 0) dpos_ = (dpos_ + 1) % lat_;
        }
        // 2. product core (in place -> wet)
        if constexpr (AcceptsSidechain<Core>::value) {
            if (sc != nullptr && scCh > 0) core_.processWithSidechain(ch, nch, n, sc, scCh);
            else core_.process(ch, nch, n);
        } else {
            (void)sc; (void)scCh;
            core_.process(ch, nch, n);
        }
        // 3.. gains, segmented at the 100 ms loudness block boundaries
        int i = 0;
        while (i < n) {
            const int seg = std::min(n - i, untilBlock_);
            const float* dp[2] = {dry_[0].data() + i, dry_[static_cast<size_t>(nch - 1)].data() + i};
            const float* wp[2] = {ch[0] + i, ch[nch - 1] + i};
            dryMeter_.process(dp, nch, seg);
            wetMeter_.process(wp, nch, seg);
            for (int k = i; k < i + seg; ++k) {
                const double a = agc_.next(), o = out_.next(), dm = deltaMix_.next(), im = inMix_.next(), mx = mix_.next();
                for (int c = 0; c < nch; ++c) {
                    const double d = dry_[static_cast<size_t>(c)][static_cast<size_t>(k)];
                    const double w = mx >= 1.0 ? ch[c][k] * a : d + mx * (ch[c][k] * a - d);  // Auto gain, then Mix
                    double y = w * o;
                    if (dm > 0.0) y += dm * ((w - d) * o - y);       // Δ: changed part only
                    if (im <= 0.0) y = d;                            // In off: dry, bit exact
                    else if (im < 1.0) y = d + im * (y - d);
                    if (std::abs(y) < 1e-30) y = 0.0;                // never emit subnormals
                    ch[c][k] = static_cast<float>(y);
                }
            }
            i += seg;
            untilBlock_ -= seg;
            if (untilBlock_ == 0) { untilBlock_ = block_; updateAutoGain(); }
        }
    }

private:
    static double dbToGain(double db) { return std::pow(10.0, db / 20.0); }

    void updateAutoGain() {
        const double ld = dryMeter_.shortTerm(), lw = wetMeter_.shortTerm();
        if (ld > kAutoGainGateLufs && lw > -199.0) {
            const double target = std::clamp(ld - lw, -kAutoGainLimitDb, kAutoGainLimitDb);
            agcDb_ += (target - agcDb_) * (1.0 - std::exp(-0.1 / kAutoGainTimeS));
        }
        if (autoGain_) agc_.setTarget(dbToGain(agcDb_));
    }

    Core core_;
    double fs_ = 48000.0, agcDb_ = 0.0;
    int nch_ = 2, lat_ = 0, dpos_ = 0, block_ = 4800, untilBlock_ = 4800;
    bool in_ = true, delta_ = false, autoGain_ = false;
    std::vector<std::vector<float>> delay_, dry_;
    LoudnessMeter dryMeter_, wetMeter_;
    LinearSmoother out_, agc_, inMix_, deltaMix_, mix_ = initialised(1.0);
    static LinearSmoother initialised(double v) { LinearSmoother s; s.reset(48000.0, 20.0, v); return s; }
};

}  // namespace sw
