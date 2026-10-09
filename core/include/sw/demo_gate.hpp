// SW AUDIO core — the demo gate: without a licence the plug-ins play with silence put in (the owner's decision 2026-10-09: 無音を挟む).
// Design values: 3 s of silence every 60 s, the first from 30 s after the plug-in starts playing (a first listen is clean; within a minute
// the gap is unmistakable), 10 ms linear fades on both sides (no clicks). The schedule counts samples from the first prepare(); a later
// prepare(fs, true) (the host activates the plug-in again) keeps the time played (2026-10-09). The block size changes nothing; outside the gaps the gain is exactly 1 (the sound is untouched to the bit). No allocation; audio thread.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sw {

class DemoGate {
public:
    static constexpr double kFirstS = 30.0, kPeriodS = 60.0, kGapS = 3.0, kFadeS = 0.010;

    // keepTime: the time already played carries over (at the new rate); otherwise it starts again
    void prepare(double fs, bool keepTime = false) {
        const double played = keepTime && fs_ > 0.0 ? static_cast<double>(pos_) / fs_ : 0.0;
        fs_ = fs;
        first_ = std::llround(kFirstS * fs);
        period_ = std::max<int64_t>(1, std::llround(kPeriodS * fs));
        gap_ = std::llround(kGapS * fs);
        fade_ = std::max<int64_t>(1, std::llround(kFadeS * fs));
        pos_ = std::llround(played * fs);
    }
    int64_t position() const { return pos_; }

    // the gain at a sample since prepare()
    float gainAt(int64_t s) const {
        if (s < first_) return 1.0f;
        const int64_t u = (s - first_) % period_;
        if (u >= gap_) return 1.0f;
        if (u < fade_) return 1.0f - static_cast<float>(u) / static_cast<float>(fade_);
        if (u < gap_ - fade_) return 0.0f;
        return static_cast<float>(u - (gap_ - fade_) + 1) / static_cast<float>(fade_);   // the mirror of the fade out: 1/f .. 1
    }

    // in place, every channel the same gain
    void process(float* const* ch, int nch, int n) {
        if (n <= 0) return;
        // most blocks lie wholly outside a gap: nothing to do
        const int64_t a = pos_, b = pos_ + n;
        pos_ = b;
        if (b <= first_) return;
        const int64_t ua = a < first_ ? -1 : (a - first_) % period_;
        const int64_t ub = (b - 1 - first_) % period_;
        const bool clear = a >= first_ && ua >= gap_ && ub >= gap_ && ub >= ua;   // inside one cycle, after its gap
        if (clear) return;
        for (int i = 0; i < n; ++i) {
            const float g = gainAt(a + i);
            if (g == 1.0f) continue;
            for (int c = 0; c < nch; ++c) if (ch[c]) ch[c][i] *= g;
        }
    }

private:
    double fs_ = 0.0;
    int64_t first_ = 0, period_ = 1, gap_ = 0, fade_ = 1, pos_ = 0;
};

}  // namespace sw
