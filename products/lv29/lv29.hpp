// SW LV29 Interp Mix — the mix of the venue sound and a simultaneous interpreter (spec: 仕様書 v1.0「LV29 Interp Mix」). Reported delay 0; no Auto gain, no Delta.
//   The main input is the venue (the floor); the interpreter comes in on the second (sidechain) input (**the spec's "or SW Link" is not here: no SW Link yet**). Without a sidechain the floor passes as it is.
//   Output: Floor (the floor only), Interp and floor (the interpreter at Interp level plus the floor, which is lowered by Floor under while the interpreter speaks), Interp (the interpreter only).
//   Auto detect On (EVO, class B): the floor is lowered only while sw::VoiceDetector hears the interpreter (the same judgement as LV05); Off: it stays at Floor under whenever Output has the floor and the interpreter.
//   Crossfade (50..2000 ms): every gain change (the Output switch, the floor going down and up) takes this long (one pole, 95 % in the Crossfade). The interpreter's mono sum goes to both channels.
#pragma once
#include "sw/param.hpp"
#include "sw/voice_detect.hpp"
#include <array>
#include <vector>

namespace sw::lv29 {

enum ParamId { Output, FloorUnder, Crossfade, InterpLevel, AutoDetect, kNumParams };
enum OutId { FloorOnly = 0, InterpAndFloor = 1, InterpOnly = 2 };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { if (prepared_) { gF_ = goalFloor(); gI_ = goalInterp(); } }
    void process(float** ch, int numCh, int n) { processWithSidechain(ch, numCh, n, nullptr, 0); }
    void processWithSidechain(float** ch, int numCh, int n, const float* const* sc, int scCh);
    int latencySamples() const { return 0; }
    bool interpreterSpeaking() const { return speaking_; }
    double floorGainDb() const { return 20.0 * std::log10(std::max(gF_, 1e-9)); }

private:
    double goalFloor() const;
    double goalInterp() const;
    double fs_ = 48000.0, gF_ = 1.0, gI_ = 0.0;
    static constexpr int kPiece = 32;   // the grid of the stream on which the interpreter's activity is read
    int ph_ = 0;
    bool prepared_ = false, speaking_ = false;
    std::array<double, kNumParams> target_{};
    VoiceDetector vd_;
    std::vector<float> mono_;
};

}  // namespace sw::lv29
