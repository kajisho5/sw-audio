#include "lv19/lv19.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv19 {
namespace { constexpr double kFps[5] = {24.0, 25.0, 30000.0 / 1001.0, 30.0, 60000.0 / 1001.0}; }

double framesPerSecond(int i) { return kFps[std::clamp(i, 0, 4)]; }

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv19.fps",   "Frame rate",    0, 4, 2,    Curve::Step, 1, {0, 1, 2, 3, 4}, "", {"24", "25", "29.97", "30", "59.94"}},
        {"lv19.delay", "Delay",         0, 1000, 0, Curve::Lin, 1, {}, "ms"},
        {"lv19.lock",  "Lock to video", 0, 1, 1,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

double Processor::effectiveDelayMs() const {
    const double d = target_[Delay];
    if (target_[LockToVideo] < 0.5) return d;
    const double fm = frameMs(); return std::min(1000.0, std::round(d / fm) * fm);
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    size_t sz = 1; while (sz < static_cast<size_t>(1.0 * fs_) + 4096) sz <<= 1;
    for (auto& b : buf_) b.assign(sz, 0.0f);
    mask_ = sz - 1; pos_ = 0; cur_ = effectiveDelayMs() * 0.001 * fs_;
    monoN_ = static_cast<size_t>(2.0 * fs_) + 1; mono_.assign(monoN_, 0.0f); scratch_.assign(monoN_, 0.0f); monoPos_ = 0; total_ = 0;
    state_ = Idle; late_ = false; writePending_ = false; prepared_ = true;
}
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); }

void Processor::markVideoClap(double reactionMs) {
    if (!prepared_) return;
    markIndex_ = total_ - static_cast<long long>(std::llround(reactionMs * 0.001 * fs_)); waitLeft_ = static_cast<long long>(0.5 * fs_); state_ = Waiting; late_ = false; writePending_ = false;
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    const int nc = std::min(numCh, 2);
    const double goal = effectiveDelayMs() * 0.001 * fs_, a = 1.0 - std::exp(-1.0 / (0.05 * fs_));
    for (int i = 0; i < n; ++i) {
        double m = 0; for (int c = 0; c < nc; ++c) m += ch[c][i]; mono_[monoPos_] = static_cast<float>(m / nc); monoPos_ = (monoPos_ + 1) % monoN_; ++total_;
        cur_ += a * (goal - cur_); if (std::abs(goal - cur_) < 1e-4) cur_ = goal;
        const size_t w = pos_ & mask_;
        const double rp = static_cast<double>(pos_) - cur_, fl = std::floor(rp), fr = rp - fl; const size_t i0 = static_cast<size_t>(static_cast<long long>(fl)) & mask_, i1 = (i0 + 1) & mask_;
        for (int c = 0; c < nc; ++c) {
            auto& b = buf_[static_cast<size_t>(c)]; b[w] = ch[c][i];
            double y = cur_ == 0.0 ? static_cast<double>(ch[c][i]) : (fr == 0.0 ? static_cast<double>(b[i0]) : (1.0 - fr) * b[i0] + fr * b[i1]);
            const float o = static_cast<float>(y); ch[c][i] = std::abs(o) < 1e-30f ? 0.0f : o;
        }
        ++pos_;
        if (state_ == Waiting && --waitLeft_ <= 0) {
            // the audio clap: the loudest sharp transient of the last 2 s
            const long long span = std::min<long long>(static_cast<long long>(monoN_) - 1, total_);
            std::vector<float>& tmp = scratch_;
            long long best = 0; double bv = 0;
            for (long long k = 0; k < span; ++k) { const size_t idx = (monoPos_ + monoN_ - static_cast<size_t>(span) + static_cast<size_t>(k)) % monoN_; const float v = std::abs(mono_[idx]); tmp[static_cast<size_t>(k)] = v; if (v > bv) { bv = v; best = k; } }
            std::nth_element(tmp.begin(), tmp.begin() + static_cast<long>(span / 2), tmp.begin() + static_cast<long>(span)); const double med = tmp[static_cast<size_t>(span / 2)];
            if (span < static_cast<long long>(0.5 * fs_) || bv < 1e-3 || bv < 12.0 * med) { state_ = Failed; }
            else {
                const long long audioIdx = total_ - span + best; foundMs_ = static_cast<double>(markIndex_ - audioIdx) / fs_ * 1000.0;
                if (foundMs_ >= 0.0) { state_ = Done; late_ = false; writePending_ = true; } else { state_ = Done; late_ = true; }
            }
        }
    }
}

int Processor::takeParamWrite(int& id, double& plain) {
    if (!writePending_) return 0;
    writePending_ = false; id = Delay; plain = std::clamp(std::round(foundMs_ * 10.0) / 10.0, 0.0, 1000.0); return 7;
}

}  // namespace sw::lv19
