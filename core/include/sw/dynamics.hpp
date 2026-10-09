// SW AUDIO core — dynamics building blocks (DY / MS / LV)
//   GainComputer   static curve: threshold / ratio / soft knee  (dB in -> gain change dB)
//   Ballistics     attack / release one-pole on a gain change (dB)
//   LevelDetector  peak / RMS / program level
//   TruePeakDetector  polyphase windowed-sinc interpolation (4x / 8x) -> inter-sample peak per input sample
//   PeakLimiter    look-ahead brick-wall limiter: sliding minimum + equal-length moving average, so the
//                  gain has fully reached its target when the peak leaves the delay line (never overshoots).
//                  In true-peak mode the gain is also held flat for the interpolation half-width (M samples)
//                  on both sides of a peak: an inter-sample peak depends on its neighbours, not on one sample.
#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace sw {

struct GainComputer {
    static constexpr double kInfinity = std::numeric_limits<double>::infinity();
    void set(double thresholdDb, double ratio, double kneeDb) {
        t_ = thresholdDb;
        slope_ = std::isinf(ratio) ? -1.0 : 1.0 / std::max(1.0, ratio) - 1.0;
        w_ = std::max(0.0, kneeDb);
    }
    double gainDb(double x) const {
        const double over = x - t_;
        if (w_ <= 0.0) return over <= 0.0 ? 0.0 : slope_ * over;
        if (2.0 * over < -w_) return 0.0;
        if (2.0 * std::abs(over) <= w_) { const double k = over + w_ / 2.0; return slope_ * k * k / (2.0 * w_); }
        return slope_ * over;
    }
    double t_ = 0, slope_ = 0, w_ = 0;
};

struct Ballistics {  // attack while the target goes down (more reduction), release while it goes up
    void set(double fs, double attackMs, double releaseMs) {
        a_ = coef(fs, attackMs);
        r_ = coef(fs, releaseMs);
    }
    void setRelease(double fs, double releaseMs) { r_ = coef(fs, releaseMs); }
    double process(double target) {
        const double c = target < y_ ? a_ : r_;
        y_ = target + c * (y_ - target);
        return y_;
    }
    void reset(double v = 0.0) { y_ = v; }
    double value() const { return y_; }
    static double coef(double fs, double ms) { return ms <= 0.0 ? 0.0 : std::exp(-1.0 / (ms * 0.001 * fs)); }
    double a_ = 0, r_ = 0, y_ = 0;
};

struct LevelDetector {
    enum class Mode { Peak, Rms, Program };
    void set(double fs, Mode m) { mode_ = m; c_ = Ballistics::coef(fs, 10.0); cp_ = Ballistics::coef(fs, 50.0); ce_ = Ballistics::coef(fs, 10.0); }
    double process(double x) {
        const double a = std::abs(x);
        pe_ = std::max(a, ce_ * pe_);  // peak envelope: instant rise, 10 ms fall (no dips at zero crossings)
        if (mode_ == Mode::Peak) return pe_;
        ms_ = x * x + c_ * (ms_ - x * x);
        const double rms = std::sqrt(ms_);
        if (mode_ == Mode::Rms) return rms;
        // program: blend toward the peak envelope as the crest factor rises (transient material)
        pk_ = std::max(a, cp_ * pk_);
        const double crest = rms > 1e-12 ? 20.0 * std::log10(pk_ / rms) : 0.0;
        const double w = std::clamp((crest - 3.0) / 9.0, 0.0, 1.0);
        return rms + w * (pe_ - rms);
    }
    void reset() { ms_ = 0; pk_ = 0; pe_ = 0; }
    Mode mode_ = Mode::Peak;
    double c_ = 0, cp_ = 0, ce_ = 0, ms_ = 0, pk_ = 0, pe_ = 0;
};

class TruePeakDetector {
public:
    static constexpr int kTapsPerPhase = 16;
    void setup(int factor) {
        constexpr double kPi = 3.14159265358979323846;
        up_ = std::max(1, factor);
        const int n = up_ * kTapsPerPhase;
        h_.assign(static_cast<size_t>(n), 0.0);
        const double centre = (n - 1) / 2.0, beta = 8.0;
        auto i0 = [](double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
        for (int i = 0; i < n; ++i) {
            const double t = (i - centre) / up_;  // in input samples
            const double sinc = std::abs(t) < 1e-12 ? 1.0 : std::sin(kPi * t) / (kPi * t);
            const double r = (i - centre) / centre;
            const double w = i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0(beta);  // Kaiser
            h_[static_cast<size_t>(i)] = sinc * w;
        }
        hist_.assign(static_cast<size_t>(kTapsPerPhase), 0.0);
        pos_ = 0;
    }
    // returns the largest absolute value among the interpolated points around input sample n - latency
    double process(double x) {
        hist_[static_cast<size_t>(pos_)] = x;
        double peak = 0;
        for (int k = 0; k < up_; ++k) {
            double acc = 0;
            int idx = pos_;
            for (int j = 0; j < kTapsPerPhase; ++j) {
                acc += h_[static_cast<size_t>(k + j * up_)] * hist_[static_cast<size_t>(idx)];
                idx = idx == 0 ? kTapsPerPhase - 1 : idx - 1;
            }
            peak = std::max(peak, std::abs(acc));
        }
        pos_ = (pos_ + 1) % kTapsPerPhase;
        return peak;
    }
    int latencySamples() const { return kTapsPerPhase / 2; }
    void reset() { std::fill(hist_.begin(), hist_.end(), 0.0); }

private:
    int up_ = 4, pos_ = 0;
    std::vector<double> h_, hist_;
};

class PeakLimiter {
public:
    // lookahead: samples; truePeak: detect inter-sample peaks with `factor`x interpolation
    void prepare(double fs, int numCh, int lookahead, bool truePeak, int factor) {
        fs_ = fs;
        nch_ = std::clamp(numCh, 1, 2);
        L_ = std::max(1, lookahead);
        tp_ = truePeak;
        tpd_.assign(static_cast<size_t>(nch_), TruePeakDetector{});
        for (auto& d : tpd_) d.setup(factor);
        D_ = tp_ ? tpd_[0].latencySamples() : 0;
        M_ = tp_ ? TruePeakDetector::kTapsPerPhase / 2 : 0;
        delay_.assign(static_cast<size_t>(nch_), std::vector<float>(static_cast<size_t>(L_ + D_ + M_), 0.0f));
        dpos_ = 0;
        ch_.assign(static_cast<size_t>(nch_), State{});
        for (auto& s : ch_) { s.qv.assign(static_cast<size_t>(L_ + 2 * M_ + 2), 1.0); s.qi.assign(static_cast<size_t>(L_ + 2 * M_ + 2), 0); s.box.assign(static_cast<size_t>(L_), 1.0); s.sum = L_; }
        n_ = 0; sustained_ = 0; autoRel_ = false;
    }
    void set(double ceilingDb, double releaseMs, double link) {
        ceil_ = std::pow(10.0, ceilingDb / 20.0);
        rel_ = Ballistics::coef(fs_, releaseMs); autoRel_ = false;   // a fixed release; setAutoRelease() after this turns the automatic one on
        link_ = std::clamp(link, 0.0, 1.0);
    }
    void setReleaseMs(double ms) { rel_ = Ballistics::coef(fs_, ms); autoRel_ = false; }
    // Auto release: short reductions recover with fastMs, ones that have lasted (gain below -1 dB for more than 100 ms) with slowMs. Decided per sample, so it does not depend on how the host cuts the audio
    void setAutoRelease(double fastMs, double slowMs) {
        autoRel_ = true; relFast_ = Ballistics::coef(fs_, fastMs); relSlow_ = Ballistics::coef(fs_, slowMs); sustainLimit_ = static_cast<long long>(0.1 * fs_);
        rel_ = sustained_ > sustainLimit_ ? relSlow_ : relFast_;
    }
    int latencySamples() const { return L_ + D_ + M_; }
    double gainReductionDb() const { return 20.0 * std::log10(std::max(1e-9, lastGain_)); }
    long long limitEvents() const { return events_; }

    void process(float** x, int numCh, int n) {
        const int nch = std::min(numCh, nch_);
        double gt[2];
        for (int i = 0; i < n; ++i) {
            double pk[2] = {0, 0};
            for (int c = 0; c < nch; ++c) pk[c] = tp_ ? tpd_[static_cast<size_t>(c)].process(x[c][i]) : std::abs(x[c][i]);
            const double pmax = nch > 1 ? std::max(pk[0], pk[1]) : pk[0];
            const double glink = pmax > ceil_ ? ceil_ / pmax : 1.0;
            bool limiting = false;
            for (int c = 0; c < nch; ++c) {
                const double gown = pk[c] > ceil_ ? ceil_ / pk[c] : 1.0;
                gt[c] = gown + link_ * (glink - gown);
                State& s = ch_[static_cast<size_t>(c)];
                // sliding minimum over the last L+2M+1 targets (monotonic queue)
                const size_t cap = s.qv.size();
                while (s.count > 0 && s.qv[(s.head + s.count - 1) % cap] >= gt[c]) --s.count;
                s.qv[(s.head + s.count) % cap] = gt[c]; s.qi[(s.head + s.count) % cap] = n_; ++s.count;
                while (s.qi[s.head] < n_ - L_ - 2 * M_) { s.head = (s.head + 1) % cap; --s.count; }
                const double m = s.qv[s.head];
                // release only lets the gain rise slowly; drops follow the minimum immediately
                s.r = m < s.r ? m : m + rel_ * (s.r - m);
                // moving average of length L -> smooth attack that is complete when the peak arrives
                s.sum += s.r - s.box[static_cast<size_t>(s.bpos)];
                s.box[static_cast<size_t>(s.bpos)] = s.r;
                s.bpos = (s.bpos + 1) % L_;
                if (++s.sinceRecalc >= 4096) { s.sum = 0; for (double v : s.box) s.sum += v; s.sinceRecalc = 0; }
                const double g = std::min(1.0, s.sum / L_);
                // delayed audio (L + detector latency + margin)
                auto& d = delay_[static_cast<size_t>(c)];
                const double xd = d[static_cast<size_t>(dpos_)];
                d[static_cast<size_t>(dpos_)] = x[c][i];
                double y = xd * g;
                y = std::clamp(y, -ceil_, ceil_);  // last-resort safety (sample peak)
                x[c][i] = static_cast<float>(y);
                if (g < 0.999) limiting = true;
                if (c == 0) lastGain_ = g;
            }
            if (autoRel_) { sustained_ = lastGain_ < 0.8912509381337456 ? sustained_ + 1 : 0; rel_ = sustained_ > sustainLimit_ ? relSlow_ : relFast_; }   // -1 dB
            if (limiting && !wasLimiting_) ++events_;
            wasLimiting_ = limiting;
            dpos_ = (dpos_ + 1) % (L_ + D_ + M_);
            ++n_;
        }
    }

private:
    struct State {
        std::vector<double> qv, box;
        std::vector<long long> qi;
        size_t head = 0, count = 0;
        double r = 1.0, sum = 0;
        int bpos = 0, sinceRecalc = 0;
    };
    double fs_ = 48000, ceil_ = 1, rel_ = 0, link_ = 1, lastGain_ = 1, relFast_ = 0, relSlow_ = 0;
    int nch_ = 2, L_ = 1, D_ = 0, M_ = 0, dpos_ = 0;
    bool tp_ = false, wasLimiting_ = false, autoRel_ = false;
    long long n_ = 0, events_ = 0, sustained_ = 0, sustainLimit_ = 0;
    std::vector<TruePeakDetector> tpd_;
    std::vector<std::vector<float>> delay_;
    std::vector<State> ch_;
};

}  // namespace sw
