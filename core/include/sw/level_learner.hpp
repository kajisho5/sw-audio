// SW AUDIO core — "set the input level" (CS03 Stepped Strip; spec: 進化機能 区分 A): it listens to the input for a few seconds, measures the average level (RMS over the stretches where something
// is playing; pauses are left out, or a source with long gaps would be turned up) and the peak, and gives the gain that brings the average to a target and the peak not over a ceiling:
//   gain = min(target RMS - measured RMS, ceiling peak - measured peak)
// so a steady source (a bass) ends at the target RMS and a peaky one (drums) is held by the ceiling, which is how the target differs from one source to another. Per sample: the result does not depend on
// how the host cuts the audio. Allocates nothing.
#pragma once
#include <algorithm>
#include <cmath>

namespace sw {

class LevelLearner {
public:
    struct Result { bool ok = false; double rmsDb = -120.0, peakDb = -120.0, gainDb = 0.0; };
    static constexpr double kTargetRmsDb = -18.0;   // spec: the average after the pre-amp
    static constexpr double kCeilingPeakDb = -6.0;  // spec: the peak not over this
    static constexpr double kSilenceDb = -60.0;     // a 10 ms stretch below this is a pause
    static constexpr double kMinActiveSeconds = 0.5;

    void prepare(double fs) { fs_ = fs; frameLen_ = std::max(1L, std::lround(0.01 * fs)); cancel(); }
    void start(double seconds) { cancel(); started_ = true; total_ = left_ = std::max<long>(1, static_cast<long>(seconds * fs_)); }
    bool learning() const { return left_ > 0; }
    double progress() const { return started_ && total_ > 0 ? 1.0 - static_cast<double>(left_) / static_cast<double>(total_) : 0.0; }

    // one sample of each channel (a mono input passes it twice)
    void add(double l, double r) {
        if (left_ <= 0) return;
        --left_;
        frameSum_ += 0.5 * (l * l + r * r);
        peak_ = std::max({peak_, std::abs(l), std::abs(r)});
        if (++frameN_ >= frameLen_) {
            const double p = frameSum_ / static_cast<double>(frameN_);
            if (p > std::pow(10.0, kSilenceDb / 10.0)) { activeSum_ += p; ++activeFrames_; }
            frameSum_ = 0.0; frameN_ = 0;
        }
    }

    Result finish() {
        Result r;
        if (!started_) return r;
        started_ = false; left_ = 0;
        if (activeFrames_ * 0.01 < kMinActiveSeconds) return r;   // nothing (or too little) was playing
        r.rmsDb = 10.0 * std::log10(activeSum_ / static_cast<double>(activeFrames_)); r.peakDb = 20.0 * std::log10(std::max(peak_, 1e-9));
        r.gainDb = std::min(kTargetRmsDb - r.rmsDb, kCeilingPeakDb - r.peakDb);
        r.ok = true;
        return r;
    }

private:
    void cancel() { started_ = false; left_ = total_ = 0; frameSum_ = activeSum_ = peak_ = 0.0; frameN_ = 0; activeFrames_ = 0; }
    double fs_ = 48000.0, frameSum_ = 0.0, activeSum_ = 0.0, peak_ = 0.0;
    long frameLen_ = 480, frameN_ = 0, activeFrames_ = 0, left_ = 0, total_ = 0;
    bool started_ = false;
};

}  // namespace sw
