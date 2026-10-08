// SW AUDIO core — feedback delay network reverb engine (RV01 Hall, RV02 Plate, RV05 Chamber, RV06 Shimmer, RV08 Gated ...)
//   N (<= 16) modulated delay lines, a Householder feedback matrix (lossless, I - 2/N * 1 1^T), per line a gain for the decay time and a one-pole
//   low-pass for the high-frequency damping. Input: one mono sample, spread over the lines with a +-1 pattern; output: two decorrelated sums (Walsh rows).
//   Line lengths glide (0.05 sample per sample) so that Size can move without clicks. Freeze sets the loop gain to 0.99995 with no damping and closes the input.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sw {

class Fdn {
public:
    static constexpr int kMax = 16;
    // an optional processor on the lines' outputs (RV06's pitch shifters): process(line, s) returns what feeds back and goes to the output
    struct LineHook { virtual double process(int line, double s) = 0; virtual ~LineHook() = default; };
    void setHook(LineHook* h) { hook_ = h; }
    void prepare(double fs, int lines, double maxSeconds) {
        fs_ = fs; n_ = std::clamp(lines, 4, kMax);
        size_t sz = 16; while (sz < static_cast<size_t>(maxSeconds * fs) + 16) sz <<= 1;
        mask_ = sz - 1;
        for (int i = 0; i < n_; ++i) { buf_[static_cast<size_t>(i)].assign(sz, 0.0f); len_[static_cast<size_t>(i)] = target_[static_cast<size_t>(i)] = 1000.0 + 37.0 * i; lp_[static_cast<size_t>(i)] = 0; g_[static_cast<size_t>(i)] = 0.5; phase_[static_cast<size_t>(i)] = static_cast<double>(i) / n_; }
        pos_ = 0; ctl_ = 0; freezeGain_ = 0.0; inGain_ = 1.0;
        setDamping(8000.0); setModulation(4.0, 0.3);
        for (int i = 0; i < n_; ++i) {
            const size_t k = static_cast<size_t>(i);
            sgnIn_[k] = ((i * 7 + 3) % 5 < 2) ? -1.0 : 1.0;
            sgnIn2_[k] = ((i * 5 + 2) % 7 < 3) ? -1.0 : 1.0;   // the right input's pattern (stereo-in plates)
            // Walsh rows 1 and 2 (1 is the all-ones row, which would only pass the mean): sequency patterns with different periods
            sgnL_[k] = ((i >> 1) & 1) ? -1.0 : 1.0;
            sgnR_[k] = ((i & 1) ^ ((i >> 2) & 1)) ? -1.0 : 1.0;
        }
    }
    int lines() const { return n_; }
    // mean of the target line lengths in seconds. The energy of the impulse response is about kEnergyConstant x decay / this (energy keeps circulating,
    // and a shorter loop hands it out more often): used to scale the late reverb to unit energy whatever the algorithm, size and decay
    double meanLengthSeconds() const { double a = 0; for (int i = 0; i < n_; ++i) a += target_[static_cast<size_t>(i)]; return a / n_ / fs_; }
    static constexpr double kEnergyConstant = 0.002;   // seconds (measured: Hall at the default size gives 0.033 x decay for a 60 ms mean length)
    void clear() { for (int i = 0; i < n_; ++i) { std::fill(buf_[static_cast<size_t>(i)].begin(), buf_[static_cast<size_t>(i)].end(), 0.0f); lp_[static_cast<size_t>(i)] = 0; } }
    // target lengths in samples (fractional); the lines glide there
    void setLength(int i, double samples) { target_[static_cast<size_t>(i)] = std::max(4.0, samples); }
    void snapLengths() { len_ = target_; }
    void setDecay(double rt60Seconds) { rt60_ = std::max(0.05, rt60Seconds); }
    void setDamping(double hz) { const double f = std::clamp(hz, 200.0, 0.45 * fs_); damp_ = std::exp(-2.0 * 3.14159265358979323846 * f / fs_); }
    void setModulation(double depthSamples, double rateHz) { modDepth_ = depthSamples; modInc_ = rateHz / fs_; rot_ = false; }
    // the same modulation from rotating phasors instead of a sine per line and sample (IN07: about half the cost of the network);
    // opt-in, so the products that use the sine stay bit-identical. Renormalised every 32 samples.
    void setFastModulation(double depthSamples, double rateHz) {
        modDepth_ = depthSamples; modInc_ = rateHz / fs_; rot_ = true;
        const double w = 6.283185307179586 * modInc_; rc_ = std::cos(w); rs_ = std::sin(w);
        for (int i = 0; i < n_; ++i) { const size_t k = static_cast<size_t>(i); pc_[k] = std::cos(6.283185307179586 * phase_[k]); ps_[k] = std::sin(6.283185307179586 * phase_[k]); }
    }
    void setFreeze(bool on) { freeze_ = on; }
    // one mono input sample -> two output samples
    void process(double in, double& outL, double& outR) { run(in, in, false, outL, outR); }
    // two inputs, injected with different +-1 patterns -> two outputs
    void processStereo(double inL, double inR, double& outL, double& outR) { run(inL, inR, true, outL, outR); }

private:
    void run(double in, double in2, bool stereo, double& outL, double& outR) {
        if (ctl_ == 0) {
            ctl_ = 32; updateGains();
            if (rot_) for (int i = 0; i < n_; ++i) { const size_t k = static_cast<size_t>(i); const double m = 1.0 / std::sqrt(pc_[k] * pc_[k] + ps_[k] * ps_[k]); pc_[k] *= m; ps_[k] *= m; }
        }
        --ctl_;
        const double fz = freezeGain_ += ((freeze_ ? 1.0 : 0.0) - freezeGain_) * 0.0005;
        const double inG = inGain_ += ((freeze_ ? 0.0 : 1.0) - inGain_) * 0.002;
        const double p = damp_ * (1.0 - fz);
        std::array<double, kMax> s;
        double sum = 0, l = 0, r = 0;
        for (int i = 0; i < n_; ++i) {
            const size_t k = static_cast<size_t>(i);
            const double step = std::clamp(target_[k] - len_[k], -0.05, 0.05); len_[k] += step;
            double d;
            if (rot_) { const double c = pc_[k] * rc_ - ps_[k] * rs_; ps_[k] = ps_[k] * rc_ + pc_[k] * rs_; pc_[k] = c; d = len_[k] + modDepth_ * ps_[k]; }
            else { phase_[k] += modInc_; if (phase_[k] >= 1.0) phase_[k] -= 1.0; d = len_[k] + modDepth_ * std::sin(6.283185307179586 * phase_[k]); }
            const double y = read(k, d);
            lp_[k] += (1.0 - p) * (y - lp_[k]);
            s[k] = lp_[k] * (g_[k] + (0.99995 - g_[k]) * fz);
            if (hook_) s[k] = hook_->process(i, s[k]);
            sum += s[k]; l += sgnL_[k] * s[k]; r += sgnR_[k] * s[k];
        }
        const double mean2 = 2.0 * sum / n_, invN = inG / std::sqrt(static_cast<double>(n_));
        for (int i = 0; i < n_; ++i) { const size_t k = static_cast<size_t>(i); const double inj = stereo ? (sgnIn_[k] * in + sgnIn2_[k] * in2) * invN : sgnIn_[k] * in * invN; buf_[k][pos_ & mask_] = static_cast<float>(s[k] - mean2 + inj); }
        ++pos_;
        const double norm = 1.0 / std::sqrt(static_cast<double>(n_));
        outL = l * norm; outR = r * norm;
    }

    double read(size_t k, double delay) const {   // 4-point Hermite
        const double rp = static_cast<double>(pos_) - delay;
        const double fl = std::floor(rp); const double f = rp - fl;
        const size_t i1 = static_cast<size_t>(static_cast<long long>(fl)) & mask_;
        const auto& b = buf_[k];
        const double y0 = b[(i1 - 1) & mask_], y1 = b[i1], y2 = b[(i1 + 1) & mask_], y3 = b[(i1 + 2) & mask_];
        const double c0 = y1, c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }
    void updateGains() { for (int i = 0; i < n_; ++i) { const size_t k = static_cast<size_t>(i); g_[k] = std::pow(10.0, -3.0 * len_[k] / (fs_ * rt60_)); } }
    double fs_ = 48000.0, rt60_ = 2.0, damp_ = 0.5, modDepth_ = 4.0, modInc_ = 0.0, freezeGain_ = 0.0, inGain_ = 1.0;
    int n_ = 16, ctl_ = 0;
    LineHook* hook_ = nullptr;
    bool freeze_ = false, rot_ = false;
    double rc_ = 1.0, rs_ = 0.0;
    std::array<double, kMax> pc_{}, ps_{};
    size_t mask_ = 0, pos_ = 0;
    std::array<std::vector<float>, kMax> buf_;
    std::array<double, kMax> len_{}, target_{}, lp_{}, g_{}, phase_{}, sgnIn_{}, sgnIn2_{}, sgnL_{}, sgnR_{};
};

}  // namespace sw
