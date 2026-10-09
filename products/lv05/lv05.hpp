// SW LV05 Auto ducker — ducks the program under a key signal (spec: 仕様書 v1.0「LV05 Auto ducker」). Reported delay 0.
//   Key: the external sidechain (a second input appears on its own). **The SW Link instance choice of the spec is not here (no SW Link yet).** Without a key signal nothing is ducked.
//   Depth 0..-40 dB, Attack (how fast the gain goes down), Hold (after the key stops), Release (back up). Gain moves in dB with one-pole ballistics.
//   Voice only (EVO, class B, first stage "rules"): the key counts only while sw::VoiceDetector says voiced speech or singing (periodic, low zero-crossing rate, over the noise floor); claps, knocks and steady noise do not duck.
//   Voice only Off: the key counts when its level is over max(-50 dBFS, floor + 10 dB) (the spec has no threshold parameter; this is the design value).
//   Hold to duck: while it is on the duck is forced (not automatable; the screen sends it while the button is held).
#pragma once
#include "sw/param.hpp"
#include "sw/voice_detect.hpp"
#include <array>
#include <vector>

namespace sw::lv05 {

enum ParamId { Depth, Attack, Hold, Release, VoiceOnly, HoldToDuck, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n) { run(ch, numCh, n, nullptr, 0); }
    void processWithSidechain(float** ch, int numCh, int n, const float* const* sc, int scCh) { run(ch, numCh, n, sc, scCh); }
    int latencySamples() const { return 0; }
    double gainDb() const { return gDb_; }
    bool keyActive() const { return keyOn_; }
    const VoiceDetector& voice() const { return vd_; }

private:
    void run(float** ch, int numCh, int n, const float* const* sc, int scCh);
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    VoiceDetector vd_;
    double gDb_ = 0, env_ = 0, floor_ = 1e-4, holdLeft_ = 0;
    bool keyOn_ = false, ducking_ = false;
    static constexpr int kPiece = 64;   // the key decision grid of the stream
    int ph_ = 0;
    double pkAcc_ = 0;
    std::vector<float> mono_;
};

}  // namespace sw::lv05
