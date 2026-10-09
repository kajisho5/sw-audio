// SW AUDIO core — ITU-R BS.1770 loudness (K-weighting, momentary 400 ms, short-term 3 s)
// Coefficients are derived from the analog prototypes so that any sample rate works;
// at 48 kHz they reproduce the tables in BS.1770. Used by Auto gain, meters (MT01/LV23) and MS01.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sw {

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
    double process(double x) {  // transposed direct form II
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
};

class KWeighting {
public:
    void setup(double fs) {
        constexpr double kPi = 3.14159265358979323846;
        {   // stage 1: high shelf (head effects)
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan(kPi * f0 / fs);
            const double Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            s_.b0 = (Vh + Vb * K / Q + K * K) / a0;
            s_.b1 = 2.0 * (K * K - Vh) / a0;
            s_.b2 = (Vh - Vb * K / Q + K * K) / a0;
            s_.a1 = 2.0 * (K * K - 1.0) / a0;
            s_.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {   // stage 2: RLB high-pass
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan(kPi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            h_.b0 = 1.0; h_.b1 = -2.0; h_.b2 = 1.0;
            h_.a1 = 2.0 * (K * K - 1.0) / a0;
            h_.a2 = (1.0 - K / Q + K * K) / a0;
        }
        reset();
    }
    double process(double x) { return h_.process(s_.process(x)); }
    void reset() { s_.reset(); h_.reset(); }
    const Biquad& shelf() const { return s_; }
    const Biquad& highpass() const { return h_; }

private:
    Biquad s_, h_;
};

class LoudnessMeter {
public:
    void setup(double fs, int numCh) {
        k_.assign(static_cast<size_t>(std::max(1, numCh)), KWeighting{});
        for (auto& k : k_) k.setup(fs);
        blockLen_ = std::max(1, static_cast<int>(std::lround(fs * 0.1)));  // 100 ms blocks
        blocks_.assign(30, 0.0);
        reset();
    }
    void reset() {
        for (auto& k : k_) k.reset();
        std::fill(blocks_.begin(), blocks_.end(), 0.0);
        acc_ = 0; inBlock_ = 0; head_ = 0; filled_ = 0;
    }
    void process(const float* const* ch, int numCh, int n) {
        numCh = std::min(numCh, static_cast<int>(k_.size()));
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < numCh; ++c) {
                const double y = k_[static_cast<size_t>(c)].process(ch[c][i]);
                acc_ += y * y;  // channel weight 1 for L/R
            }
            if (++inBlock_ == blockLen_) {
                blocks_[head_] = acc_ / blockLen_;
                head_ = (head_ + 1) % blocks_.size();
                filled_ = std::min(filled_ + 1, blocks_.size());
                acc_ = 0; inBlock_ = 0;
            }
        }
    }
    double momentary() const { return lufs(meanOfLast(4)); }
    double shortTerm() const { return lufs(meanOfLast(30)); }
    // mean square of the last n complete 100 ms blocks (fewer while starting up)
    double meanOfLast(size_t n) const {
        n = std::min(n, filled_);
        if (n == 0) return 0.0;
        double s = 0;
        for (size_t i = 0; i < n; ++i) s += blocks_[(head_ + blocks_.size() - 1 - i) % blocks_.size()];
        return s / static_cast<double>(n);
    }
    static double lufs(double meanSquare) { return meanSquare > 1e-20 ? -0.691 + 10.0 * std::log10(meanSquare) : -200.0; }

private:
    std::vector<KWeighting> k_;
    std::vector<double> blocks_;
    double acc_ = 0;
    int blockLen_ = 4800, inBlock_ = 0;
    size_t head_ = 0, filled_ = 0;
};

// Loudness range (EBU Tech 3342): short-term values (the caller adds one every 100 ms once 3 s of signal are there) in 0.1 LU bins; LRA = the 95th - the 10th percentile of what is above the
// relative gate (the power mean - 20 LU). Values at or below the absolute gate (-70 LUFS) are not counted.
class LoudnessRange {
public:
    LoudnessRange() { reset(); }
    void reset() { count_.assign(kBins, 0.0); sum_.assign(kBins, 0.0); }
    void add(double shortTermLufs) {
        if (!(shortTermLufs > -70.0)) return;
        const size_t b = std::min(kBins - 1, static_cast<size_t>(std::lround((shortTermLufs + 70.0) * 10.0)));
        count_[b] += 1.0; sum_[b] += std::pow(10.0, (shortTermLufs + 0.691) / 10.0);
    }
    double range() const {
        double c = 0, s = 0; for (size_t b = 0; b < kBins; ++b) { c += count_[b]; s += sum_[b]; }
        if (c < 1.0) return 0.0;
        const double gate = LoudnessMeter::lufs(s / c) - 20.0;
        double total = 0; for (size_t b = 0; b < kBins; ++b) if (bin(b) >= gate) total += count_[b];
        if (total < 1.0) return 0.0;
        auto pct = [&](double p) { double acc = 0; for (size_t b = 0; b < kBins; ++b) { if (bin(b) < gate) continue; acc += count_[b]; if (acc >= p * total) return bin(b); } return 0.0; };
        return pct(0.95) - pct(0.10);
    }

private:
    static constexpr size_t kBins = 701;   // -70.0 .. 0.0 LUFS in 0.1 LU steps
    static double bin(size_t b) { return -70.0 + 0.1 * static_cast<double>(b); }
    std::vector<double> count_, sum_;
};

// BS.1770-4 gated integrated loudness: 400 ms blocks every 100 ms, absolute gate -70 LUFS, relative gate -10 LU.
// Block energies go into 0.1 LU histogram bins (fixed memory, any length). forgetSeconds > 0 lets old blocks fade out
// (exponential weight per block) so that a controller can follow a level change; 0 keeps everything (plain integrated).
class IntegratedLoudness {
public:
    void setup(double fs, int numCh, double forgetSeconds) {
        k_.assign(static_cast<size_t>(std::max(1, numCh)), KWeighting{});
        for (auto& k : k_) k.setup(fs);
        hop_ = std::max(1, static_cast<int>(std::lround(fs * 0.1)));
        decay_ = forgetSeconds > 0.0 ? std::exp(-0.1 / forgetSeconds) : 1.0;
        count_.assign(kBins, 0.0); sum_.assign(kBins, 0.0);
        reset();
    }
    void reset() {
        for (auto& k : k_) k.reset();
        std::fill(count_.begin(), count_.end(), 0.0); std::fill(sum_.begin(), sum_.end(), 0.0);
        acc_ = 0; inHop_ = 0; hops_.fill(0.0); nHops_ = 0; blocks_ = 0;
    }
    void process(const float* const* ch, int numCh, int n) {
        numCh = std::min(numCh, static_cast<int>(k_.size()));
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < numCh; ++c) { const double y = k_[static_cast<size_t>(c)].process(ch[c][i]); acc_ += y * y; }
            if (++inHop_ == hop_) {
                for (int k = 3; k > 0; --k) hops_[static_cast<size_t>(k)] = hops_[static_cast<size_t>(k - 1)];
                hops_[0] = acc_ / hop_; acc_ = 0; inHop_ = 0;
                if (++nHops_ >= 4) addBlock(0.25 * (hops_[0] + hops_[1] + hops_[2] + hops_[3]));
            }
        }
    }
    // gated integrated loudness in LUFS (-200 when nothing above the absolute gate has been measured)
    double integrated() const {
        double c = 0, s = 0;
        for (size_t b = 0; b < kBins; ++b) { c += count_[b]; s += sum_[b]; }
        if (c <= 0.0) return -200.0;
        const double relGate = LoudnessMeter::lufs(s / c) - 10.0;
        double c2 = 0, s2 = 0;
        for (size_t b = 0; b < kBins; ++b) if (binLufs(b) >= relGate) { c2 += count_[b]; s2 += sum_[b]; }
        return c2 > 0.0 ? LoudnessMeter::lufs(s2 / c2) : -200.0;
    }
    long long blocks() const { return blocks_; }

private:
    static constexpr size_t kBins = 701;   // -70.0 .. 0.0 LUFS in 0.1 LU steps (louder blocks go into the top bin)
    static double binLufs(size_t b) { return -70.0 + 0.1 * static_cast<double>(b); }
    void addBlock(double ms) {
        const double l = LoudnessMeter::lufs(ms);
        if (decay_ < 1.0) for (size_t b = 0; b < kBins; ++b) { count_[b] *= decay_; sum_[b] *= decay_; }
        ++blocks_;
        if (l < -70.0) return;   // absolute gate
        const size_t b = std::min(kBins - 1, static_cast<size_t>(std::lround((l + 70.0) * 10.0)));
        count_[b] += 1.0; sum_[b] += ms;
    }
    std::vector<KWeighting> k_;
    std::vector<double> count_, sum_;
    std::array<double, 4> hops_{};
    double acc_ = 0, decay_ = 1.0;
    int hop_ = 4800, inHop_ = 0, nHops_ = 0;
    long long blocks_ = 0;
};

}  // namespace sw
