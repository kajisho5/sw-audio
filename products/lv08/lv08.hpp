// SW LV08 Room Noise — room noise suppressor (spec: 仕様書 v1.0「LV08 Room Noise」). Reported delay 256 samples (5.3 ms at 48 kHz: one 256-point frame; sqrt-Hann, hop 64).
//   HVAC (steady noise): a per-bin noise estimate (minimum follower of the power smoothed over about 14 frames, x1.5 to make up for the bias of a minimum: down at once, up 3 dB/s; or the profile of Learn noise) and a spectral gain G = 1 - a N / P limited to Reduction
//   (a = 1.0 / 1.6 / 2.4 for Sensitivity Low / Mid / High); the gain moves down in one frame and up over about 8 frames, which keeps musical noise down.
//   Voice guard: while sw::VoiceDetector hears a voice, 200 Hz..4 kHz may not be cut deeper than -30 / -10 / -5 dB (Low / Mid / High).
//   Keyboard: a jump of the energy above 2 kHz by 12 dB over its 200 ms average while no voice is active is a key strike: for 53 ms everything over 1.5 kHz is lowered by Reduction.
//   Learn noise (button, not a parameter): learnNoise() averages the power of the next 2 s per bin and uses it instead of the follower until clearLearned(). Not saved with the project.
#pragma once
#include "sw/param.hpp"
#include "sw/stft.hpp"
#include "sw/voice_detect.hpp"
#include <array>
#include <vector>

namespace sw::lv08 {

enum ParamId { Reduction, Sensitivity, VoiceGuard, Keyboard, Hvac, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor : public Stft::Handler {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return kLatency; }
    void learnNoise() { if (prepared_) { learning_ = true; learnFrames_ = 0; std::fill(acc_.begin(), acc_.end(), 0.0); } }
    void clearLearned() { learned_ = false; learning_ = false; }
    bool learning() const { return learning_; }
    bool hasLearned() const { return learned_; }
    double lastMinGainDb() const { return minGDb_; }
    bool keyboardActive() const { return kbHold_ > 0; }
    void frame(std::complex<double>* const* spec, int nch, int nbins) override;
    static constexpr int kLatency = 256, kHop = 64;

private:
    double fs_ = 48000.0;
    bool prepared_ = false, learning_ = false, learned_ = false;
    std::array<double, kNumParams> target_{};
    Stft stft_;
    VoiceDetector vd_;
    std::vector<double> ps_, pn_, nt_, gs_, learnedP_, acc_;
    int learnFrames_ = 0, warm_ = 0, kbHold_ = 0;
    double hfAvg_ = 0, minGDb_ = 0, riseF_ = 1.0;
    std::vector<float> mono_;
};

}  // namespace sw::lv08
