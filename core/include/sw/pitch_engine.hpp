// SW AUDIO core — pitch engine for the voice products (VO01 Tune, VO02 Tune Rt, VO03 Harmony, VO06 Formant): a pitch analyser and a time-domain PSOLA synthesiser.
//   PitchAnalyzer: the input goes into a ring; every hop (2.7 ms at 48 kHz) a YIN-type period estimate (coarse on a 12 kHz copy, refined at the full rate to 0.05 sample by a normalised
//   correlation with parabolic interpolation) fills a pitch track (period, voiced). Epoch-like "marks" are laid one period apart (m(i+1) = m(i) + P, then moved by up to +-P/20 to the position where
//   the waveform of one period matches the previous one best): pitch-synchronous grain centres that keep the same phase of the cycle. Without a pitch the marks run at a fixed 5 ms.
//   PsolaSynth: reads the analyser's marks. For synthesis marks s(k+1) = s(k) + P / ratio it takes the nearest analysis mark, cuts a Hann grain of `windowPeriods` periods round it (resampled by the
//   formant factor: the grain is read at that speed, so the spectral envelope moves by it, and it is played that much shorter) and overlap-adds it; the sum is divided by the sum of the windows
//   at each sample (so the level does not depend on how much the grains overlap: more when the pitch goes up, less when it goes down). Grains of neighbouring marks differ in phase when the pitch moves
//   (the harmonics add only partly), which costs up to 3.5 dB at the extremes: the output is brought back to the input's level by a slow gain (80 ms, at most +-6 dB).
//   Unvoiced stretches use ratio 1 (the input comes back delayed). A RatioSource gives (ratio, formant) at every synthesis mark from the pitch at the analysis mark.
//   Latency = 2 x the longest period + 256 samples (the right half of the last grain and the pitch window must be in): minF0 85 Hz -> about 1400 samples at 48 kHz. Below minF0 nothing is shifted.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sw {

struct PitchConfig {
    double fs = 48000.0, minF0 = 85.0, maxF0 = 1000.0, windowPeriods = 2.0;
};

class PitchAnalyzer {
public:
    struct Frame { double period = 0.0; bool voiced = false; };
    static constexpr int kHopBase = 128;

    void prepare(const PitchConfig& c) {
        cfg_ = c; fs_ = c.fs;
        pMax_ = static_cast<int>(std::ceil(fs_ / c.minF0)); pMin_ = std::max(8, static_cast<int>(std::floor(fs_ / c.maxF0)));
        unvoiced_ = static_cast<int>(std::lround(0.005 * fs_));
        hop_ = std::max(32, static_cast<int>(std::lround(kHopBase * fs_ / 48000.0)));
        // the right half of the last grain (windowPeriods / 2 periods) plus the pitch window (the pitch is known for times up to w - Pmax) plus the hop and a margin
        latency_ = static_cast<int>(std::lround(0.5 * c.windowPeriods * pMax_ + pMax_ + 2 * hop_ + 64));
        size_t sz = 1 << 14; while (sz < static_cast<size_t>(8 * pMax_ + 4096)) sz <<= 1;
        ring_.assign(sz, 0.0f); mask_ = sz - 1;
        decim_ = std::max(1, static_cast<int>(std::lround(fs_ / 12000.0)));
        fd_ = fs_ / decim_;
        coarse_.assign(static_cast<size_t>(4 * pMax_ / decim_ + 64), 0.0); cpos_ = 0;
        scratch_.assign(static_cast<size_t>(pMax_ / decim_ + 16), 0.0); scratch2_ = scratch_;   // no allocation in the audio thread afterwards
        track_.reserve(512); marks_.reserve(2048);
        lpC_ = std::exp(-2.0 * 3.14159265358979323846 * 1500.0 / fs_);
        reset();
    }
    void reset() {
        std::fill(ring_.begin(), ring_.end(), 0.0f); std::fill(coarse_.begin(), coarse_.end(), 0.0);
        w_ = 0; since_ = 0; dphase_ = 0; lp1_ = lp2_ = 0.0; track_.clear(); marks_.clear(); trackEnd_ = -1.0; cpos_ = 0;
        voiced_ = false; lastPeriod_ = unvoiced_; nextMark_ = 0.0; haveMark_ = false;
    }
    static int latencyFor(const PitchConfig& c) {
        const int pMax = static_cast<int>(std::ceil(c.fs / c.minF0)), hop = std::max(32, static_cast<int>(std::lround(kHopBase * c.fs / 48000.0)));
        return static_cast<int>(std::lround(0.5 * c.windowPeriods * pMax + pMax + 2 * hop + 64));
    }
    int latency() const { return latency_; }
    int maxPeriod() const { return pMax_; }
    int minPeriod() const { return pMin_; }
    int unvoicedPeriod() const { return unvoiced_; }
    double sampleRate() const { return fs_; }
    int hop() const { return hop_; }
    int64_t now() const { return w_; }
    double windowPeriods() const { return cfg_.windowPeriods; }
    void push(double x) {
        ring_[static_cast<size_t>(w_) & mask_] = static_cast<float>(x);
        ++w_;
        lp1_ = x + lpC_ * (lp1_ - x); lp2_ = lp1_ + lpC_ * (lp2_ - lp1_);
        if (++dphase_ >= decim_) { dphase_ = 0; coarse_[static_cast<size_t>(cpos_)] = lp2_; cpos_ = (cpos_ + 1) % static_cast<int>(coarse_.size()); ++cn_; }
        if (++since_ >= hop_) { since_ = 0; analyse(); extendMarks(); }
    }
    double sample(int64_t i) const { return i < 0 || i >= w_ || w_ - i > static_cast<int64_t>(ring_.size()) - 4 ? 0.0 : ring_[static_cast<size_t>(i) & mask_]; }
    // 4-point Hermite read at a fractional position (zero outside what is stored)
    double read(double pos) const {
        const double fl = std::floor(pos), f = pos - fl; const int64_t i = static_cast<int64_t>(fl);
        const double y0 = sample(i - 1), y1 = sample(i), y2 = sample(i + 1), y3 = sample(i + 2);
        return y1 + 0.5 * f * (y2 - y0 + f * (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3 + f * (3.0 * (y1 - y2) + y3 - y0)));
    }
    double trackEnd() const { return trackEnd_; }
    Frame trackAt(double t) const {
        Frame f; f.period = unvoiced_;
        if (track_.empty()) return f;
        // the entries are in time order: find the last one at or before t
        size_t lo = 0, hi = track_.size();
        while (lo + 1 < hi) { const size_t mid = (lo + hi) / 2; if (track_[mid].t <= t) lo = mid; else hi = mid; }
        const Entry& a = track_[lo]; const Entry& b = track_[std::min(lo + 1, track_.size() - 1)];
        f.voiced = (t - a.t < b.t - t || &a == &b) ? a.voiced : b.voiced;
        if (a.voiced && b.voiced && b.t > a.t) { const double u = std::clamp((t - a.t) / (b.t - a.t), 0.0, 1.0); f.period = a.period + u * (b.period - a.period); }
        else f.period = f.voiced ? (a.voiced ? a.period : b.period) : unvoiced_;
        return f;
    }
    double markEnd() const { return marks_.empty() ? -1.0 : marks_.back().t; }
    // the mark nearest to t (false when there is none yet); period / voicing as they were laid
    bool nearestMark(double t, double& mark, double& period, bool& voiced) const {
        if (marks_.empty()) return false;
        size_t lo = 0, hi = marks_.size();
        while (lo + 1 < hi) { const size_t mid = (lo + hi) / 2; if (marks_[mid].t <= t) lo = mid; else hi = mid; }
        size_t k = lo; if (lo + 1 < marks_.size() && marks_[lo + 1].t - t < t - marks_[lo].t) k = lo + 1;
        mark = marks_[k].t; period = marks_[k].period; voiced = marks_[k].voiced;
        return true;
    }
    double f0Hz(double t) const { const Frame f = trackAt(t); return f.voiced && f.period > 0.0 ? fs_ / f.period : 0.0; }
    double currentF0() const { return voiced_ && lastPeriod_ > 0.0 ? fs_ / lastPeriod_ : 0.0; }   // the newest estimate
    bool currentVoiced() const { return voiced_; }
    const std::vector<double>& debugDn() const { return scratch2_; }   // the last normalised difference function (coarse lags), for the tests

private:
    struct Entry { double t, period; bool voiced; };
    struct Mark { double t, period; bool voiced; };
    void analyse() {
        // YIN on the coarse copy: the newest 2 Pmax_d samples
        const int Pd = pMax_ / decim_ + 2, W = Pd, N = static_cast<int>(coarse_.size());
        const int need = W + Pd + 2;
        const double tCenter = static_cast<double>(w_) - static_cast<double>(pMax_);   // centre of the 2 x Pmax stretch the estimate looks at (input time)
        if (cn_ < need) return;
        auto at = [&](int back) { return coarse_[static_cast<size_t>(((cpos_ - 1 - back) % N + N) % N)]; };   // back = 0 is the newest
        const int tMin = std::max(2, static_cast<int>(pMin_ / decim_)), tMax = Pd;
        std::vector<double>& d = scratch_; d.assign(static_cast<size_t>(tMax + 2), 0.0);
        double e = 0.0; for (int j = 0; j < W; ++j) e += at(j) * at(j);
        for (int tau = 1; tau <= tMax; ++tau) { double s = 0.0; for (int j = 0; j < W; ++j) { const double v = at(j) - at(j + tau); s += v * v; } d[static_cast<size_t>(tau)] = s; }
        double run = 0.0; std::vector<double>& dn = scratch2_; dn.assign(static_cast<size_t>(tMax + 2), 1.0);
        for (int tau = 1; tau <= tMax; ++tau) { run += d[static_cast<size_t>(tau)]; dn[static_cast<size_t>(tau)] = run > 0.0 ? d[static_cast<size_t>(tau)] * tau / run : 1.0; }
        Entry en{tCenter, 0.0, false};
        const bool loud = e / W > 1e-7;   // -70 dB re full scale rms (after the low-pass)
        if (loud) {
            int best = 0;
            for (int tau = tMin; tau <= tMax - 1; ++tau) if (dn[static_cast<size_t>(tau)] < 0.05) { while (tau + 1 <= tMax - 1 && dn[static_cast<size_t>(tau + 1)] < dn[static_cast<size_t>(tau)]) ++tau; best = tau; break; }
            if (best == 0) { double m = 1e9; for (int tau = tMin; tau <= tMax - 1; ++tau) if (dn[static_cast<size_t>(tau)] < m) { m = dn[static_cast<size_t>(tau)]; best = tau; } if (m > 0.3) best = 0; }
            if (best > 0) {   // an octave error is the commonest: prefer a sub-multiple of the lag whose dip is about as deep
                const double db = dn[static_cast<size_t>(best)];
                for (int k = 2; k <= 4; ++k) {
                    const int lk = static_cast<int>(std::lround(static_cast<double>(best) / k));
                    if (lk < tMin || lk + 1 > tMax - 1) continue;
                    int bl = lk; for (int q = lk - 1; q <= lk + 1; ++q) if (dn[static_cast<size_t>(q)] < dn[static_cast<size_t>(bl)]) bl = q;
                    if (bl >= tMin && dn[static_cast<size_t>(bl)] < 0.3 && dn[static_cast<size_t>(bl)] <= db + 0.05) { best = bl; break; }
                }
            }
            if (best > 0) {
                double P = refine(best * decim_, tCenter);
                if (P >= pMin_ && P <= pMax_) { en.period = P; en.voiced = true; }
            }
        }
        // hysteresis: a short gap in a voiced stretch does not drop the voicing
        voiced_ = en.voiced; if (en.voiced) lastPeriod_ = en.period;
        track_.push_back(en); trackEnd_ = en.t;
        while (track_.size() > 256) track_.erase(track_.begin(), track_.begin() + 64);
    }
    // full-rate refinement: normalised cross-correlation of the period-long block ending at the window centre against the block one lag earlier, over lag +-(decim + 1)
    double refine(int P0, double centre) {
        const int64_t c = static_cast<int64_t>(centre);
        const int span = std::max(P0, 32);
        double bestV = -2.0; int bestL = P0; double vals[3] = {0, 0, 0};
        std::array<double, 32> cors{}; int nc = 0; const int lo = std::max(pMin_, P0 - decim_ - 1), hi = std::min(pMax_, std::min(P0 + decim_ + 1, lo + 31));
        for (int L = lo; L <= hi; ++L) {
            double r = 0, e1 = 0, e2 = 0;
            for (int j = -span / 2; j < span / 2; ++j) { const double a = sample(c + j), b = sample(c + j - L); r += a * b; e1 += a * a; e2 += b * b; }
            const double v = r / std::sqrt(e1 * e2 + 1e-30);
            cors[static_cast<size_t>(nc++)] = v;
            if (v > bestV) { bestV = v; bestL = L; }
        }
        const int k = bestL - lo;
        double P = bestL;
        if (k > 0 && k + 1 < nc) { vals[0] = cors[static_cast<size_t>(k - 1)]; vals[1] = cors[static_cast<size_t>(k)]; vals[2] = cors[static_cast<size_t>(k + 1)]; const double den = vals[0] - 2.0 * vals[1] + vals[2]; if (std::abs(den) > 1e-12) P += std::clamp(0.5 * (vals[0] - vals[2]) / den, -1.0, 1.0); }
        return bestV > 0.5 ? P : 0.0;
    }
    void extendMarks() {
        if (trackEnd_ < 0.0) return;
        if (!haveMark_) { nextMark_ = std::max(0.0, trackEnd_ - 2.0 * pMax_); haveMark_ = true; marks_.push_back({std::floor(nextMark_), static_cast<double>(unvoiced_), false}); }
        for (int guard = 0; guard < 8; ++guard) {
            const Mark& last = marks_.back();
            const Frame f = trackAt(last.t);
            const double P = f.voiced ? f.period : unvoiced_;
            double cand = last.t + P;
            const double R = f.voiced ? std::max(2.0, P / 20.0) : 0.0;
            if (cand + R > trackEnd_ || cand + P / 2 + R + 2 > static_cast<double>(w_)) return;
            if (f.voiced && last.voiced) {
                // move the mark to where one period of the waveform matches the previous period best
                double bestV = -2.0, bestD = 0.0; const int half = static_cast<int>(P / 2);
                for (int dd = -static_cast<int>(R); dd <= static_cast<int>(R); ++dd) {
                    double r = 0, e1 = 0, e2 = 0;
                    for (int j = -half; j < half; ++j) { const double a = sample(static_cast<int64_t>(std::lround(cand)) + dd + j), b = sample(static_cast<int64_t>(std::lround(last.t)) + j); r += a * b; e1 += a * a; e2 += b * b; }
                    const double v = r / std::sqrt(e1 * e2 + 1e-30);
                    if (v > bestV) { bestV = v; bestD = dd; }
                }
                cand = std::round(cand) + bestD;
            }
            marks_.push_back({cand, P, f.voiced});
            while (marks_.size() > 1024) marks_.erase(marks_.begin(), marks_.begin() + 256);
        }
    }
    PitchConfig cfg_;
    double fs_ = 48000.0, lpC_ = 0.0, lp1_ = 0.0, lp2_ = 0.0, fd_ = 12000.0, trackEnd_ = -1.0, lastPeriod_ = 240.0, nextMark_ = 0.0;
    int pMax_ = 565, pMin_ = 48, unvoiced_ = 240, hop_ = 128, latency_ = 0, decim_ = 4, dphase_ = 0, since_ = 0, cpos_ = 0;
    int64_t w_ = 0, cn_ = 0;
    size_t mask_ = 0;
    bool voiced_ = false, haveMark_ = false;
    std::vector<float> ring_;
    std::vector<double> coarse_, scratch_, scratch2_;
    std::vector<Entry> track_;
    std::vector<Mark> marks_;
};

class RatioSource {
public:
    virtual ~RatioSource() = default;
    // at a synthesis mark: the pitch at the analysis mark (0 / false when unvoiced) and the time since the previous synthesis mark -> the pitch factor and the formant factor (1 = keep)
    virtual void ratio(double f0Hz, bool voiced, double dtSeconds, double& pitchRatio, double& formantRatio) = 0;
};

class PsolaSynth {
public:
    void prepare(const PitchAnalyzer& a) {
        size_t sz = 1 << 13; while (sz < static_cast<size_t>(8 * a.maxPeriod() + 2048)) sz <<= 1;
        acc_.assign(sz, 0.0f); wacc_.assign(sz, 0.0f); mask_ = sz - 1;
        reset(a);
    }
    void reset(const PitchAnalyzer&) { std::fill(acc_.begin(), acc_.end(), 0.0f); std::fill(wacc_.begin(), wacc_.end(), 0.0f); nextS_ = -1.0; emitted_ = -1; lastRatio_ = 1.0; lastPOut_ = 0.0; pending_ = false; inE_ = outE_ = 0.0; gain_ = 1.0; }
    // call once per input sample, right after PitchAnalyzer::push: returns the output sample for input time now - 1 - latency
    double process(const PitchAnalyzer& a, RatioSource& rs) {
        const int64_t n = a.now() - 1;                        // the newest input sample
        const int64_t tau = n - a.latency();                  // the output sample to emit
        if (tau < 0) return 0.0;
        for (int guard = 0; guard < 64; ++guard) {
            if (nextS_ < 0.0) { const double end = a.markEnd(); if (end < 0.0) break; nextS_ = static_cast<double>(std::max<int64_t>(0, tau)); }
            // the factors are asked for once per mark (a pending mark waits here until its grain is due)
            if (!pending_) {
                if (!a.nearestMark(nextS_, pMark_, pPeriod_, pVoiced_) || a.markEnd() < nextS_ + 0.5 * pPeriod_) break;
                double r = 1.0, fm = 1.0;
                const double dt = lastPOut_ > 0.0 ? lastPOut_ / a.sampleRate() : pPeriod_ / a.sampleRate();
                if (pVoiced_) rs.ratio(a.sampleRate() / pPeriod_, true, dt, r, fm); else rs.ratio(0.0, false, dt, r, fm);
                if (!pVoiced_ || !(r > 0.0)) { r = 1.0; fm = 1.0; }
                pR_ = std::clamp(r, 0.25, 4.0); pFm_ = std::clamp(fm, 0.4, 2.5);
                pending_ = true;
            }
            const double mark = pMark_, period = pPeriod_, r = pR_, fm = pFm_;
            const double hw = 0.5 * a.windowPeriods() * period;        // half-length of the grain in the input
            const double hOut = hw / fm;                                // half-length in the output
            if (nextS_ - hOut > static_cast<double>(tau)) break;        // not yet needed
            const double pOut = period / r;
            const int64_t i0 = static_cast<int64_t>(std::ceil(nextS_ - hOut)), i1 = static_cast<int64_t>(std::floor(nextS_ + hOut));
            for (int64_t i = i0; i <= i1; ++i) {
                if (i <= emitted_) continue;                            // already gone
                const double v = (static_cast<double>(i) - nextS_) / hOut;            // -1 .. 1
                const double win = 0.5 * (1.0 + std::cos(3.14159265358979323846 * v));
                acc_[static_cast<size_t>(i) & mask_] += static_cast<float>(win * a.read(mark + (static_cast<double>(i) - nextS_) * fm));
                wacc_[static_cast<size_t>(i) & mask_] += static_cast<float>(win);
            }
            nextS_ += pOut; lastPOut_ = pOut; pending_ = false;
            lastRatio_ = r;
        }
        const size_t at = static_cast<size_t>(tau) & mask_;
        const double ws = wacc_[at];
        const double y = ws > 1e-3 ? acc_[at] / std::max(ws, 0.35) : 0.0;
        acc_[at] = 0.0f; wacc_[at] = 0.0f; emitted_ = tau;
        // level: the input (delayed to the same time) against the output, smoothed over 80 ms
        const double k = 1.0 - std::exp(-1.0 / (0.08 * a.sampleRate())), xi = a.sample(tau);
        inE_ += k * (xi * xi - inE_); outE_ += k * (y * y - outE_);
        const double want = std::clamp(std::sqrt((inE_ + 1e-12) / (outE_ + 1e-12)), 0.5, 2.0);
        gain_ += 0.002 * (want - gain_);
        return y * gain_;
    }
    double lastRatio() const { return lastRatio_; }

private:
    std::vector<float> acc_, wacc_;
    size_t mask_ = 0;
    double pMark_ = 0.0, pPeriod_ = 240.0, pR_ = 1.0, pFm_ = 1.0;
    bool pVoiced_ = false, pending_ = false;
    double nextS_ = -1.0, lastRatio_ = 1.0, lastPOut_ = 0.0, inE_ = 0.0, outE_ = 0.0, gain_ = 1.0;
    int64_t emitted_ = -1;
};

}  // namespace sw
