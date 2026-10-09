#include "lv05/lv05.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace sw::lv05 {

namespace {
// the instances that can be the key: the LIVE products (LV05 itself not); 0 is the external sidechain
struct KeyProduct { const char* code; const char* label; };
const KeyProduct kKeyProducts[] = {
    {"LV01", "LV01 Voice"}, {"LV02", "LV02 Feedback"}, {"LV03", "LV03 Channel"}, {"LV04", "LV04 Safety limiter"}, {"LV06", "LV06 Stream master"}, {"LV07", "LV07 Speech Agc"}, {"LV08", "LV08 Room Noise"},
    {"LV09", "LV09 Hum Cut"}, {"LV10", "LV10 Voice Fx"}, {"LV11", "LV11 Mic Switch"}, {"LV12", "LV12 Geq 31"}, {"LV13", "LV13 Live Peq"}, {"LV14", "LV14 Align"}, {"LV15", "LV15 Auto Mixer"},
    {"LV16", "LV16 Live Gate"}, {"LV17", "LV17 Bus Comp"}, {"LV18", "LV18 Pop Guard"}, {"LV19", "LV19 Av Sync"}, {"LV20", "LV20 Rta"}, {"LV21", "LV21 Test Gen"}, {"LV22", "LV22 Polarity"},
    {"LV23", "LV23 Loudness"}, {"LV24", "LV24 Live Reverb"}, {"LV25", "LV25 Live Delay"}, {"LV26", "LV26 Mono"}, {"LV27", "LV27 Scene Sync"}, {"LV28", "LV28 Remote Hub"}, {"LV29", "LV29 Interp Mix"},
    {"LV30", "LV30 Recorder"}};
constexpr int kNumKeyProducts = static_cast<int>(sizeof(kKeyProducts) / sizeof(kKeyProducts[0]));
}  // namespace
int Processor::keyChoices() { return kNumKeyProducts + 1; }
const char* Processor::keyProduct() const { const int k = static_cast<int>(target_[Key]); return k >= 1 && k <= kNumKeyProducts ? kKeyProducts[k - 1].code : nullptr; }

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"lv05.depth",   "Depth",        -40, 0, -12,   Curve::Lin, 1, {}, "dB"},
            {"lv05.attack",  "Attack",       1, 500, 80,    Curve::Log, 1, {}, "ms"},
            {"lv05.hold",    "Hold",         0, 5, 1.2,     Curve::Lin, 1, {}, "s"},
            {"lv05.release", "Release",      0.1, 10, 2.0,  Curve::Log, 1, {}, "s"},
            {"lv05.voice",   "Voice only",   0, 1, 1,       Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"lv05.hold2duck", "Hold to duck", 0, 1, 0,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        };
        std::vector<double> steps{0}; std::vector<std::string> labels{"Sidechain"};
        for (int k = 0; k < kNumKeyProducts; ++k) { steps.push_back(k + 1); labels.push_back(kKeyProducts[k].label); }
        v.push_back({"lv05.key", "Key", 0, static_cast<double>(kNumKeyProducts), 0, Curve::Step, 1, steps, "", labels});
        v[HoldToDuck].automatable = false;
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int maxBlock) {
    fs_ = sampleRate; vd_.prepare(fs_); gDb_ = 0; env_ = 0; floor_ = 1e-4; holdLeft_ = 0; keyOn_ = false; ph_ = 0; pkAcc_ = 0; ducking_ = false;
    mono_.assign(static_cast<size_t>(std::max(1, maxBlock) + 8), 0.0f); prepared_ = true;
}
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); }

void Processor::run(float** ch, int numCh, int n, const float* const* sc, int scCh) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    if (static_cast<size_t>(n) > mono_.size()) mono_.assign(static_cast<size_t>(n), 0.0f);   // only if the host exceeds the announced block size
    const bool hasKey = sc && scCh > 0 && sc[0];
    const bool forced = target_[HoldToDuck] > 0.5;
    if (hasKey) for (int i = 0; i < n; ++i) mono_[static_cast<size_t>(i)] = scCh > 1 && sc[1] ? 0.5f * (sc[0][i] + sc[1][i]) : sc[0][i];
    // key decision per block of samples (voice detector runs on the key, frames of 10 ms)
    const double depth = std::min(0.0, target_[Depth]);
    const double aC = std::exp(-1.0 / (0.001 * target_[Attack] * fs_)), rC = std::exp(-1.0 / (target_[Release] * fs_)), holdN = target_[Hold] * fs_;
    const double envC = std::exp(-1.0 / (0.01 * fs_));
    const bool voiceOnly = target_[VoiceOnly] > 0.5;
    // the key is judged on a grid of the stream (every 64 samples, wherever the host's block starts): the detector is fed all the time, the decision of a piece is taken at its end and holds for the next one
    for (int off = 0; off < n;) {
        const int m = std::min(kPiece - ph_, n - off);
        if (hasKey) {
            vd_.process(mono_.data() + off, m);
            for (int i = 0; i < m; ++i) { const double a = std::abs(mono_[static_cast<size_t>(off + i)]); env_ = std::max(a, envC * env_); pkAcc_ = std::max(pkAcc_, env_); }
        }
        for (int i = 0; i < m; ++i) {
            const double target = ducking_ ? depth : 0.0;
            const double c = target < gDb_ ? aC : rC;
            gDb_ = target + c * (gDb_ - target);
            if (std::abs(gDb_) < 1e-4 && target == 0.0) gDb_ = 0.0;
            if (gDb_ != 0.0) { const double g = std::pow(10.0, gDb_ / 20.0); for (int c2 = 0; c2 < numCh; ++c2) { const float y = static_cast<float>(ch[c2][off + i] * g); ch[c2][off + i] = std::abs(y) < 1e-30f ? 0.0f : y; } }
        }
        off += m; ph_ += m;
        if (ph_ >= kPiece) {
            ph_ = 0;
            bool on = false;
            if (hasKey) {
                if (voiceOnly) on = vd_.active();
                else {
                    const double pk = pkAcc_;
                    if (pk < floor_) floor_ = std::max(1e-6, pk); else floor_ = std::min(floor_ * std::pow(10.0, 3.0 / 20.0 * kPiece / fs_), std::max(floor_, 0.0056));
                    on = pk > std::max(0.00316, floor_ * 3.1623);
                }
            }
            pkAcc_ = 0;
            keyOn_ = on || forced;
            if (keyOn_) holdLeft_ = holdN;
            ducking_ = keyOn_ || holdLeft_ > 0;
            if (!keyOn_ && holdLeft_ > 0) holdLeft_ -= kPiece;
        }
    }
}

}  // namespace sw::lv05
