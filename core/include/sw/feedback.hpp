// SW AUDIO core — acoustic feedback (howling) suppressor shared by LV02 Feedback, LV03 Channel and LV12 Geq 31.
//   Detection: a 4096-point Hann FFT of the mono input every 2048 samples (done on the audio thread with fixed buffers, about 0.3 ms per frame). A bin is a candidate when it is a local maximum above -70 dBFS,
//   stands out of its surroundings (bins 8..20 away) by the Sensitivity threshold and is tonal (bins 4-5 away at least 12 dB lower). A candidate is followed from frame to frame (within 1.5 bins, not dropping more than 3 dB per frame);
//   when it lasts the required number of frames it is a howl. A tone whose frequency is an integer multiple (2..6, within 2 %) of another persistent tone that is at least as loud is taken for music and is not cut.
//   Filters: up to N slots (bell filters, cut = depth, width in octaves). LIVE slots come and go on their own: a repeated detection deepens the cut (up to Max depth), a slot that has not been detected for Release seconds
//   fades out over 1 s and is freed. FIXED slots stay until cleared. Ring out: while on, detections need only 2 frames, are easier to trigger (-4 dB) and are stored as FIXED at Max depth.
#pragma once
#include "sw/fft.hpp"
#include "sw/svf.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace sw {

class FeedbackGuard {
public:
    static constexpr int kMaxSlots = 12, kFft = 4096, kHop = 2048;
    struct Slot { bool used = false, fixed = false; double freq = 1000, depthDb = 0, targetDb = 0; double age = 0; bool freeing = false; };

    void prepare(double fs, int slots) {
        fs_ = fs; nslots_ = std::clamp(slots, 1, kMaxSlots); fft_.setup(kFft);
        win_.resize(kFft); for (int i = 0; i < kFft; ++i) win_[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * i / kFft);
        ring_.assign(kFft, 0.0); buf_.assign(kFft, {}); mag_.assign(kFft / 2 + 1, 0.0);
        pos_ = 0; sinceHop_ = 0; for (auto& t : tracks_) t = Track{};
        for (auto& s : slot_) s = Slot{};
        for (auto& c : f_) for (auto& s : c) s.reset();
        applied_.fill(false); rampLeft_ = 0;
    }
    void setParams(int sensitivity, double maxDepthDb, double widthOct, double releaseS) {
        sens_ = std::clamp(sensitivity, 0, 2); maxDepth_ = std::clamp(std::abs(maxDepthDb), 0.0, 40.0); width_ = std::clamp(widthOct, 0.02, 1.0); release_ = std::max(0.1, releaseS);
        dirty_ = true;
    }
    void ringOut(bool on) { ringOut_ = on; }
    bool ringingOut() const { return ringOut_; }
    void lockFilters() { for (auto& s : slot_) if (s.used && !s.freeing) s.fixed = true; }
    void clearLive() { for (auto& s : slot_) if (s.used && !s.fixed) { s.targetDb = 0; s.freeing = true; } for (auto& t : tracks_) t = Track{}; }
    void clearAll() { for (auto& s : slot_) if (s.used) { s.targetDb = 0; s.freeing = true; s.fixed = false; } for (auto& t : tracks_) t = Track{}; }
    int slots() const { return nslots_; }
    const Slot& slot(int i) const { return slot_[static_cast<size_t>(i)]; }
    int activeCount() const { int n = 0; for (int i = 0; i < nslots_; ++i) if (slot_[static_cast<size_t>(i)].used && slot_[static_cast<size_t>(i)].targetDb > 0) ++n; return n; }
    // FIXED slots as (frequency, depth) pairs for the saved state
    std::vector<std::pair<double, double>> fixedList() const { std::vector<std::pair<double, double>> v; for (int i = 0; i < nslots_; ++i) { const Slot& s = slot_[static_cast<size_t>(i)]; if (s.used && s.fixed && !s.freeing) v.push_back({s.freq, s.targetDb}); } return v; }
    void addFixed(double freq, double depthDb) { Slot* s = allocate(); if (!s) return; *s = Slot{}; s->used = true; s->fixed = true; s->freq = std::clamp(freq, 20.0, fs_ * 0.45); s->targetDb = std::clamp(depthDb, 0.0, 40.0); }

    void process(float** ch, int numCh, int n) {
        const int nc = std::min(numCh, 2);
        // analysis
        for (int i = 0; i < n; ++i) {
            double m = ch[0][i]; if (nc > 1) m = 0.5 * (m + ch[1][i]);
            ring_[static_cast<size_t>(pos_)] = m; pos_ = (pos_ + 1) % kFft;
            if (++sinceHop_ >= kHop) { sinceHop_ = 0; analyse(); }
        }
        // slot timing and filter updates (per block)
        const double dt = static_cast<double>(n) / fs_;
        for (auto& s : slot_) {
            if (!s.used) continue;
            if (!s.fixed && !s.freeing) { s.age += dt; if (s.age > release_) { s.targetDb = 0; s.freeing = true; } }
            const double step = s.freeing ? maxDepth_ * dt / 1.0 : 40.0 * dt / 0.05;   // fade out over 1 s, fade in over 50 ms
            if (s.depthDb < s.targetDb) s.depthDb = std::min(s.targetDb, s.depthDb + step); else if (s.depthDb > s.targetDb) s.depthDb = std::max(s.targetDb, s.depthDb - std::max(step, 1.0 * dt / 0.05));
            if (s.freeing && s.depthDb <= 1e-6) { s = Slot{}; }
        }
        for (int i = 0; i < nslots_; ++i) {
            Slot& s = slot_[static_cast<size_t>(i)]; const size_t k = static_cast<size_t>(i);
            const bool on = s.used && s.depthDb > 1e-6;
            if (on && (std::abs(s.depthDb - appliedDepth_[k]) > 0.02 || std::abs(s.freq - appliedFreq_[k]) > 0.01 || dirty_ || !applied_[k])) {
                const double q = 1.0 / (2.0 * std::sinh(0.34657359027997264 * width_ * 2.0));   // ln2/2 * BW
                const bool first = !applied_[k];
                for (auto& c : f_) { if (first) { c[k].reset(); c[k].setup(Svf::Mode::Bell, s.freq, fs_, std::max(0.3, q), -s.depthDb); } else c[k].setupRamp(Svf::Mode::Bell, s.freq, fs_, std::max(0.3, q), -s.depthDb, std::max(1, n)); }
                appliedDepth_[k] = s.depthDb; appliedFreq_[k] = s.freq; applied_[k] = true;
            } else if (!on) applied_[k] = false;
        }
        dirty_ = false;
        for (int i = 0; i < nslots_; ++i) {
            const size_t k = static_cast<size_t>(i);
            if (!(slot_[k].used && slot_[k].depthDb > 1e-6) && !applied_[k]) continue;
            if (!applied_[k]) continue;
            for (int c = 0; c < nc; ++c) { float* x = ch[c]; for (int j = 0; j < n; ++j) x[j] = static_cast<float>(f_[static_cast<size_t>(c)][k].process(x[j])); }
        }
    }

private:
    struct Track { bool on = false; double freq = 0, level = -200, prom = 0; int count = 0; bool rejected = false, seen = false; };
    static constexpr int kTracks = 24;

    Slot* allocate() {
        for (int i = 0; i < nslots_; ++i) if (!slot_[static_cast<size_t>(i)].used) return &slot_[static_cast<size_t>(i)];
        Slot* oldest = nullptr; for (int i = 0; i < nslots_; ++i) { Slot& s = slot_[static_cast<size_t>(i)]; if (!s.fixed && (!oldest || s.age > oldest->age)) oldest = &s; }
        if (oldest) { const size_t k = static_cast<size_t>(oldest - slot_.data()); applied_[k] = false; for (auto& c : f_) c[k].reset(); }
        return oldest;
    }
    void register_(double freq, double prom) {
        const double depthNew = ringOut_ ? maxDepth_ : std::min(maxDepth_, std::max(6.0, prom - 4.0));
        for (int i = 0; i < nslots_; ++i) {
            Slot& s = slot_[static_cast<size_t>(i)];
            if (s.used && std::abs(std::log2(s.freq / freq)) < 1.0 / 6.0 && !s.freeing) {   // same place: refresh and deepen
                s.age = 0; if (ringOut_) s.fixed = true;
                s.targetDb = std::min(maxDepth_, std::max(s.targetDb, depthNew) + (s.fixed ? 0.0 : 3.0)); s.freq = 0.7 * s.freq + 0.3 * freq; return;
            }
        }
        Slot* s = allocate(); if (!s) return;
        *s = Slot{}; s->used = true; s->fixed = ringOut_; s->freq = freq; s->targetDb = depthNew; s->depthDb = 0;
    }
    void analyse() {
        for (int i = 0; i < kFft; ++i) buf_[static_cast<size_t>(i)] = ring_[static_cast<size_t>((pos_ + i) % kFft)] * win_[static_cast<size_t>(i)];
        fft_.forward(buf_);
        const int nb = kFft / 2;
        for (int k = 0; k <= nb; ++k) { const double m = std::abs(buf_[static_cast<size_t>(k)]) / (kFft / 4.0); mag_[static_cast<size_t>(k)] = 20.0 * std::log10(std::max(m, 1e-9)); }
        static constexpr double kProm[3] = {18.0, 14.0, 10.0}; static constexpr int kNeed[3] = {8, 5, 3};
        const double promThr = kProm[sens_] - (ringOut_ ? 4.0 : 0.0); const int need = ringOut_ ? 2 : kNeed[sens_];
        for (auto& t : tracks_) t.seen = false;
        const int kLo = std::max(24, static_cast<int>(60.0 / (fs_ / kFft))), kHi = std::min(nb - 22, static_cast<int>(fs_ * 0.45 / (fs_ / kFft)));
        for (int k = kLo; k < kHi; ++k) {
            const double m = mag_[static_cast<size_t>(k)];
            if (m < -70.0 || m < mag_[static_cast<size_t>(k - 1)] || m < mag_[static_cast<size_t>(k + 1)] || m < mag_[static_cast<size_t>(k - 2)] || m < mag_[static_cast<size_t>(k + 2)]) continue;
            double around = 0; int cnt = 0;
            for (int d = 8; d <= 20; ++d) { around += mag_[static_cast<size_t>(k - d)] + mag_[static_cast<size_t>(k + d)]; cnt += 2; }
            const double prom = m - around / cnt;
            const double side = std::max({mag_[static_cast<size_t>(k - 4)], mag_[static_cast<size_t>(k + 4)], mag_[static_cast<size_t>(k - 5)], mag_[static_cast<size_t>(k + 5)]});
            if (prom < promThr || m - side < 12.0) continue;
            const double a = mag_[static_cast<size_t>(k - 1)], b = m, c = mag_[static_cast<size_t>(k + 1)], den = a - 2 * b + c;
            const double off = std::abs(den) > 1e-9 ? std::clamp(0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
            const double freq = (k + off) * fs_ / kFft;
            Track* tr = nullptr;
            for (auto& t : tracks_) if (t.on && !t.seen && std::abs(t.freq - freq) < 1.5 * fs_ / kFft) { tr = &t; break; }
            if (tr) { if (m >= tr->level - 3.0) ++tr->count; else tr->count = 1; tr->freq = freq; tr->level = m; tr->prom = prom; tr->seen = true; }
            else for (auto& t : tracks_) if (!t.on) { t = Track{}; t.on = true; t.freq = freq; t.level = m; t.prom = prom; t.count = 1; t.seen = true; break; }
        }
        for (auto& t : tracks_) if (t.on && !t.seen) t = Track{};
        for (auto& t : tracks_) {
            if (!t.on || t.rejected || t.count < need) continue;
            for (auto& o : tracks_) {
                if (&o == &t || !o.on || o.count < 1 || o.freq >= t.freq || o.level < t.level - 0.01) continue;
                const double r = t.freq / o.freq, nr = std::round(r);
                if (nr >= 2 && nr <= 6 && std::abs(r - nr) < 0.02 * nr) { t.rejected = true; break; }
            }
            if (!t.rejected) register_(t.freq, t.prom);
        }
    }

    double fs_ = 48000; int nslots_ = 12, sens_ = 2; double maxDepth_ = 12, width_ = 0.1, release_ = 8;
    bool ringOut_ = false, dirty_ = true;
    Fft fft_; std::vector<double> win_, ring_, mag_; std::vector<std::complex<double>> buf_;
    int pos_ = 0, sinceHop_ = 0, rampLeft_ = 0;
    std::array<Track, kTracks> tracks_{};
    std::array<Slot, kMaxSlots> slot_{};
    std::array<std::array<Svf, kMaxSlots>, 2> f_{};
    std::array<double, kMaxSlots> appliedDepth_{}, appliedFreq_{};
    std::array<bool, kMaxSlots> applied_{};
};

}  // namespace sw
