// SWINGBY (SW IN07) — the effects after the voices: six slots (Drive, Chorus, Delay, Reverb, EQ, Limit) in any order. Design values (README「IN07 の設計」).
//   Each effect fades in and out over 5 ms when it is switched (no click) and costs nothing while it is off; switching one on clears its memory
//   (no old tail comes back). Parameters move at control rate (every 32 samples: filter coefficients ramp, mixes glide).
//   Drive: y = tanh(G x) / sqrt(G), G = 1 + 15 a (up to +24 dB into the curve, about the same loudness out), at 2x; Tone = one-pole low-pass
//          1.5 .. 20 kHz (log); Mix between the dry signal (through the same 2x filters, so the phases match; only computed below 100 %) and
//          the driven one.
//   Chorus: per channel two taps of a modulated delay (7 ms + up to +-4 ms x Depth), the taps half a cycle apart, the right channel a quarter
//           cycle after the left; Rate 0.05 .. 5 Hz (log); Hermite interpolation; the modulation is one rotating phasor (no sine per sample).
//   Delay: ping-pong, synced to the host tempo (1/16 .. 1/2 with dotted 1/8 and 1/4; 120 bpm without a host tempo): the mono input feeds the left
//          line, each line's output feeds the other; in the loop a one-pole high-pass at 100 Hz and low-pass at 6 kHz (each echo darker); the time
//          glides (80 ms) when the tempo moves.
//   Reverb: sw::Fdn, 8 lines of 29 .. 89 ms (Householder matrix), RT60 = Size, Damp = the lines' low-pass from 16 kHz to 1.5 kHz (log), slow
//           modulation (rotating phasors: Fdn::setFastModulation); the output is scaled to unit energy (the FDN's energy constant), so Mix sets the balance whatever the size.
//   EQ: low shelf 200 Hz, bell 1 kHz (Q 0.7), high shelf 4 kHz (Q 0.707), +-12 dB (TPT SVF).
//   Limit: Gain, then a stereo-linked peak limiter with instant attack (no look-ahead, so no latency): the gain is at most ceiling / peak at every
//          sample, so the output never passes the ceiling; it recovers with Release.
//   Mix (Drive, Chorus, Delay, Reverb) is a crossfade: dry (1 - m) + wet m.
#pragma once
#include "sw/fdn.hpp"
#include "sw/oversample.hpp"
#include "sw/svf.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sw::in07 {

enum FxId { FxDrive = 0, FxChorus, FxDelay, FxReverb, FxEq, FxLimit, kFx };

// the effects' parameters, in plain values (the Processor fills it from its parameter table)
struct FxParams {
    std::array<bool, kFx> on{};
    double driveAmount = 30, driveTone = 60, driveMix = 100;   // %
    double chorusRate = 0.2, chorusDepth = 40, chorusMix = 35;  // Hz, %, %
    double delayBeats = 0.75, delayFeedback = 35, delayMix = 22;  // beats, %, %
    double reverbSize = 2.3, reverbDamp = 40, reverbMix = 30;   // s, %, %
    double eqLow = 0, eqMid = 0, eqHigh = 0;                    // dB
    double limitGain = 0, limitCeiling = -1, limitRelease = 50; // dB, dB, ms
};

class FxChain {
public:
    static constexpr int kCtl = 32;

    void prepare(double fs, const FxParams& p) {
        fs_ = fs;
        for (auto& o : os_) o.reset();
        for (auto& o : osDry_) o.reset();
        // chorus: 7 + 4 ms at most (plus interpolation), delay: 4 s (a half note at 30 bpm)
        size_t cs = 16; while (cs < static_cast<size_t>(0.016 * fs) + 8) cs <<= 1;
        for (auto& b : chorusBuf_) b.assign(cs, 0.0f);
        chorusMask_ = cs - 1;
        size_t ds = 16; while (ds < static_cast<size_t>(4.1 * fs) + 8) ds <<= 1;
        for (auto& b : delayBuf_) b.assign(ds, 0.0f);
        delayMask_ = ds - 1;
        fdn_.prepare(fs, 8, 0.2);
        static const double kLen[8] = {0.0293, 0.0371, 0.0419, 0.0503, 0.0593, 0.0677, 0.0787, 0.0887};   // seconds: no common factors
        for (int i = 0; i < 8; ++i) fdn_.setLength(i, kLen[i] * fs);
        fdn_.snapLengths();
        fdn_.setFastModulation(3.0 * fs / 48000.0, 0.27);
        for (int f = 0; f < kFx; ++f) { gain_[static_cast<size_t>(f)] = p.on[static_cast<size_t>(f)] ? 1.0 : 0.0; clear(f); }
        delaySamples_ = -1.0; ctl_ = 0; first_ = true;
        p_ = p;
        control();
    }
    void setTempo(double bpm) { if (bpm > 0.0) bpm_ = std::clamp(bpm, 30.0, 300.0); }
    void setOrder(const std::array<int, kFx>& o) { order_ = o; }
    const std::array<int, kFx>& order() const { return order_; }
    void setParams(const FxParams& p) { p_ = p; }
    // switch states jump (no fade) when nothing has been processed since prepare
    void snapSwitches() { for (int f = 0; f < kFx; ++f) gain_[static_cast<size_t>(f)] = p_.on[static_cast<size_t>(f)] ? 1.0 : 0.0; }
    // start again from silence with the current settings (a new patch): every effect's memory cleared, switches, mixes and the delay
    // time at their targets at once. No allocation (audio thread).
    void restart() {
        for (int f = 0; f < kFx; ++f) { gain_[static_cast<size_t>(f)] = p_.on[static_cast<size_t>(f)] ? 1.0 : 0.0; clear(f); }
        for (auto& o : osDry_) o.reset();
        delaySamples_ = -1.0; ctl_ = 0; first_ = true;
        control();
        g0_ = gain_;
    }

    // in place, n samples of left and right
    // the highest peak into the limiter (after its Gain) since the last reset: how hard it works (a meter; tests and the preset tool)
    double limiterPeak() const { return limPeak_; }
    void resetLimiterPeak() { limPeak_ = 0.0; }
    void process(float* l, float* r, int n) {
        int off = 0;
        while (off < n) {
            if (ctl_ == 0) { control(); ctl_ = kCtl; }
            const int m = std::min(n - off, ctl_);
            for (int f : order_) run(f, l + off, r + off, m);
            ctl_ -= m; off += m;
        }
    }

private:
    static double fastTanh(double x) {   // [7/6] Pade approximant, as the voice's drive
        if (x > 4.97) return 1.0;
        if (x < -4.97) return -1.0;
        const double x2 = x * x;
        return x * (135135.0 + x2 * (17325.0 + x2 * (378.0 + x2))) / (135135.0 + x2 * (62370.0 + x2 * (3150.0 + 28.0 * x2)));
    }
    static double hermite(const std::vector<float>& b, size_t mask, double rp) {
        const double fl = std::floor(rp), f = rp - fl;
        const size_t i1 = static_cast<size_t>(static_cast<long long>(fl)) & mask;
        const double y0 = b[(i1 - 1) & mask], y1 = b[i1], y2 = b[(i1 + 1) & mask], y3 = b[(i1 + 2) & mask];
        const double c0 = y1, c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }
    void clear(int f) {
        switch (f) {
            case FxDrive: for (auto& o : os_) o.reset(); for (auto& o : osDry_) o.reset(); toneL_ = toneR_ = 0.0; break;
            case FxChorus: for (auto& b : chorusBuf_) std::fill(b.begin(), b.end(), 0.0f); break;
            case FxDelay: for (auto& b : delayBuf_) std::fill(b.begin(), b.end(), 0.0f); for (auto& v : dlp_) v = 0.0; for (auto& v : dhp_) v = 0.0; break;
            case FxReverb: fdn_.clear(); break;
            case FxEq: for (auto& ch : eq_) for (auto& s : ch) s.reset(); break;
            case FxLimit: limEnv_ = 1.0; break;
            default: break;
        }
    }

    void control() {
        // switches: 5 ms linear fades; an effect switched on from silence starts clean
        const double step = kCtl / (0.005 * fs_);
        for (int f = 0; f < kFx; ++f) {
            const size_t k = static_cast<size_t>(f);
            const double target = p_.on[k] ? 1.0 : 0.0;
            if (target > 0.0 && gain_[k] <= 0.0) clear(f);
            g0_[k] = gain_[k];
            gain_[k] = target > gain_[k] ? std::min(target, gain_[k] + step) : std::max(target, gain_[k] - step);
        }
        // mixes glide (one control period each step, about 10 ms to settle)
        auto glide = [&](double& v, double t) { v = first_ ? t : v + 0.5 * (t - v); };
        glide(driveMix_, p_.driveMix / 100.0); glide(chorusMix_, p_.chorusMix / 100.0); glide(delayMix_, p_.delayMix / 100.0); glide(reverbMix_, p_.reverbMix / 100.0);
        glide(driveG_, 1.0 + 15.0 * std::clamp(p_.driveAmount / 100.0, 0.0, 1.0));
        driveComp_ = 1.0 / std::sqrt(driveG_);
        if (dryStale_ && driveMix_ < 0.9999) { for (auto& o : osDry_) o.reset(); }   // the dry path starts again from silence
        glide(chorusDepth_, std::clamp(p_.chorusDepth / 100.0, 0.0, 1.0) * 0.004 * fs_);
        glide(feedback_, std::clamp(p_.delayFeedback / 100.0, 0.0, 0.9));
        glide(limGain_, std::pow(10.0, p_.limitGain / 20.0));
        glide(limCeil_, std::pow(10.0, std::min(0.0, p_.limitCeiling) / 20.0));
        const double toneHz = 1500.0 * std::pow(20000.0 / 1500.0, std::clamp(p_.driveTone / 100.0, 0.0, 1.0));
        toneC_ = 1.0 - std::exp(-2.0 * 3.141592653589793 * std::min(toneHz, 0.45 * fs_) / fs_);
        { const double w = 6.283185307179586 * std::clamp(p_.chorusRate, 0.01, 10.0) / fs_; chRc_ = std::cos(w); chRs_ = std::sin(w);
          const double m = 1.0 / std::sqrt(chC_ * chC_ + chS_ * chS_); chC_ *= m; chS_ *= m; }
        delayGlide_ = 1.0 - std::exp(-1.0 / (0.08 * fs_));
        const double target = std::clamp(p_.delayBeats, 0.0625, 4.0) * 60.0 / bpm_ * fs_;
        delayTarget_ = std::min(target, static_cast<double>(delayMask_) - 8.0);
        if (delaySamples_ < 0.0) delaySamples_ = delayTarget_;
        dlpC_ = 1.0 - std::exp(-2.0 * 3.141592653589793 * 6000.0 / fs_);
        dhpC_ = 1.0 - std::exp(-2.0 * 3.141592653589793 * 100.0 / fs_);
        fdn_.setDecay(std::clamp(p_.reverbSize, 0.1, 30.0));
        fdn_.setDamping(16000.0 * std::pow(1500.0 / 16000.0, std::clamp(p_.reverbDamp / 100.0, 0.0, 1.0)));
        reverbNorm_ = std::sqrt(fdn_.meanLengthSeconds() / (Fdn::kEnergyConstant * std::clamp(p_.reverbSize, 0.1, 30.0)));
        const int n = first_ ? 0 : kCtl;
        for (auto& ch : eq_) {
            ch[0].setupRamp(Svf::Mode::LowShelf, 200.0, fs_, 0.7071, std::clamp(p_.eqLow, -12.0, 12.0), n);
            ch[1].setupRamp(Svf::Mode::Bell, 1000.0, fs_, 0.7, std::clamp(p_.eqMid, -12.0, 12.0), n);
            ch[2].setupRamp(Svf::Mode::HighShelf, 4000.0, fs_, 0.7071, std::clamp(p_.eqHigh, -12.0, 12.0), n);
        }
        limRel_ = std::exp(-1.0 / (std::clamp(p_.limitRelease, 1.0, 5000.0) * 1e-3 * fs_));
        first_ = false;
    }

    // one effect over m samples (within a control period), faded by its switch
    void run(int f, float* l, float* r, int m) {
        const size_t k = static_cast<size_t>(f);
        const double g1 = gain_[k], ga = g0_[k];
        if (g1 <= 0.0 && ga <= 0.0) return;   // off: no cost
        const double dg = (g1 - ga) / kCtl;
        double g = ga + dg * (kCtl - ctl_);
        for (int i = 0; i < m; ++i) {
            g += dg;
            const double xl = l[i], xr = r[i];
            double yl = xl, yr = xr;
            switch (f) {
                case FxDrive: drive(xl, xr, yl, yr); break;
                case FxChorus: chorus(xl, xr, yl, yr); break;
                case FxDelay: delay(xl, xr, yl, yr); break;
                case FxReverb: reverb(xl, xr, yl, yr); break;
                case FxEq: yl = eq_[0][2].process(eq_[0][1].process(eq_[0][0].process(xl))); yr = eq_[1][2].process(eq_[1][1].process(eq_[1][0].process(xr))); break;
                case FxLimit: limit(xl, xr, yl, yr); break;
                default: break;
            }
            l[i] = static_cast<float>(xl + g * (yl - xl));
            r[i] = static_cast<float>(xr + g * (yr - xr));
        }
    }

    void drive(double xl, double xr, double& yl, double& yr) {
        const double gg = driveG_, comp = driveComp_, mix = driveMix_;
        const bool full = mix >= 0.9999;   // at 100 % the phase-matched dry path is not needed (it restarts clean when the mix comes down)
        double y[2] = {xl, xr};
        for (int c = 0; c < 2; ++c) {
            double up[2];
            os_[static_cast<size_t>(c)].up(y[c], up);
            for (double& v : up) v = fastTanh(gg * v) * comp;
            const double wet = os_[static_cast<size_t>(c)].down(up);
            double& tone = c == 0 ? toneL_ : toneR_;
            tone += toneC_ * (wet - tone);
            if (full) { y[c] = tone; continue; }
            double dry[2];
            osDry_[static_cast<size_t>(c)].up(y[c], dry);
            const double d = osDry_[static_cast<size_t>(c)].down(dry);
            y[c] = d + mix * (tone - d);
        }
        if (full) { dryStale_ = true; } else if (dryStale_) { dryStale_ = false; }
        yl = y[0]; yr = y[1];
    }

    void chorus(double xl, double xr, double& yl, double& yr) {
        const size_t w = chorusPos_ & chorusMask_;
        chorusBuf_[0][w] = static_cast<float>(xl); chorusBuf_[1][w] = static_cast<float>(xr);
        // one rotating phasor (renormalised each control period): the taps half a cycle apart are +-sin, the right channel's +-cos
        const double c = chC_ * chRc_ - chS_ * chRs_; chS_ = chS_ * chRc_ + chC_ * chRs_; chC_ = c;
        const double base = 0.007 * fs_, depth = chorusDepth_, now = static_cast<double>(chorusPos_);
        const auto& bl = chorusBuf_[0]; const auto& br = chorusBuf_[1];
        const double wl = 0.5 * (hermite(bl, chorusMask_, now - (base + depth * chS_)) + hermite(bl, chorusMask_, now - (base - depth * chS_)));
        const double wr = 0.5 * (hermite(br, chorusMask_, now - (base + depth * chC_)) + hermite(br, chorusMask_, now - (base - depth * chC_)));
        ++chorusPos_;
        yl = xl + chorusMix_ * (wl - xl);
        yr = xr + chorusMix_ * (wr - xr);
    }

    void delay(double xl, double xr, double& yl, double& yr) {
        delaySamples_ += (delayTarget_ - delaySamples_) * delayGlide_;   // glides (80 ms) when the tempo moves
        const double rp = static_cast<double>(delayPos_) - delaySamples_;
        double o[2] = {hermite(delayBuf_[0], delayMask_, rp), hermite(delayBuf_[1], delayMask_, rp)};
        for (int c = 0; c < 2; ++c) {   // the loop's filters: each echo a little darker and thinner
            dlp_[c] += dlpC_ * (o[c] - dlp_[c]);
            dhp_[c] += dhpC_ * (dlp_[c] - dhp_[c]);
            o[c] = dlp_[c] - dhp_[c];
        }
        const size_t w = delayPos_ & delayMask_;
        delayBuf_[0][w] = static_cast<float>(0.5 * (xl + xr) + feedback_ * o[1]);   // ping-pong: the input enters on the left
        delayBuf_[1][w] = static_cast<float>(feedback_ * o[0]);
        ++delayPos_;
        yl = xl + delayMix_ * (o[0] - xl);
        yr = xr + delayMix_ * (o[1] - xr);
    }

    void reverb(double xl, double xr, double& yl, double& yr) {
        // mono in, two decorrelated outputs: the FDN's stereo input with a centred (L = R) source left the wet L and R at a correlation
        // of -0.59 (a mono fold-down lost 5.9 dB of the reverb); (L + R) / sqrt 2 keeps the wet level within about 1 dB of before
        // its two outputs still lean negative (-0.04 .. -0.23 by octave band on noise): 12 % of each side into the other brings them to about +0.1
        double ol = 0.0, or_ = 0.0;
        fdn_.process((xl + xr) * 0.70710678118654752, ol, or_);
        const double a = ol + 0.12 * or_, b = or_ + 0.12 * ol;
        ol = a; or_ = b;
        yl = xl + reverbMix_ * (ol * reverbNorm_ - xl);
        yr = xr + reverbMix_ * (or_ * reverbNorm_ - xr);
    }

    void limit(double xl, double xr, double& yl, double& yr) {
        const double a = xl * limGain_, b = xr * limGain_, pk = std::max(std::abs(a), std::abs(b));
        if (pk > limPeak_) limPeak_ = pk;
        const double need = pk > limCeil_ ? limCeil_ / pk : 1.0;
        limEnv_ = need < limEnv_ ? need : need + (limEnv_ - need) * limRel_;
        yl = a * limEnv_; yr = b * limEnv_;
    }

    double fs_ = 48000.0, bpm_ = 120.0;
    FxParams p_;
    std::array<int, kFx> order_{FxDrive, FxChorus, FxDelay, FxReverb, FxEq, FxLimit};
    std::array<double, kFx> gain_{}, g0_{};
    int ctl_ = 0;
    bool first_ = true;
    // drive
    std::array<Oversampler2x, 2> os_, osDry_;
    bool dryStale_ = false;
    double driveG_ = 1.0, driveComp_ = 1.0, driveMix_ = 1.0, toneC_ = 1.0, toneL_ = 0.0, toneR_ = 0.0;
    // chorus
    std::array<std::vector<float>, 2> chorusBuf_;
    size_t chorusMask_ = 0, chorusPos_ = 0;
    double chC_ = 1.0, chS_ = 0.0, chRc_ = 1.0, chRs_ = 0.0, chorusDepth_ = 0.0, chorusMix_ = 0.0;
    // delay
    std::array<std::vector<float>, 2> delayBuf_;
    size_t delayMask_ = 0, delayPos_ = 0;
    double delaySamples_ = -1.0, delayTarget_ = 0.0, delayGlide_ = 0.0, feedback_ = 0.0, delayMix_ = 0.0, dlpC_ = 0.0, dhpC_ = 0.0;
    double dlp_[2] = {0.0, 0.0}, dhp_[2] = {0.0, 0.0};
    // reverb
    Fdn fdn_;
    double reverbMix_ = 0.0, reverbNorm_ = 1.0;
    // eq
    std::array<std::array<Svf, 3>, 2> eq_;
    // limit
    double limGain_ = 1.0, limCeil_ = 1.0, limEnv_ = 1.0, limRel_ = 0.999, limPeak_ = 0.0;
};

}  // namespace sw::in07
