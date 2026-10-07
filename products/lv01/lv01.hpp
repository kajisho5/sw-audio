// SW LV01 Voice — a one-knob voice strip (spec: 仕様書 v1.0「LV01 Voice」). Reported delay 0.
//   Chain: Noise (3-band downward expander, zero latency) -> EQ (low cut, mud cut, presence, air) -> Comp (linked, soft knee) -> Limit (-1 dBFS, instantaneous, no look-ahead).
//   Use picks the end point of every stage; Voice (0..100 %) scales them all together: stage amount = end point x Voice / 100. Voice 0 is a hard bypass (the input comes out bit for bit).
//   Noise: each band follows its own noise floor (power average over 30 ms; the floor drops at once to a lower level and rises 3 dB per second, never above -42 dBFS (per band)). Under floor + 6 dB the band is pulled down
//   by 3 dB per dB, at most the Use depth x Voice. Opens in 2 ms, closes in 80 ms.
//   Stage values (display only, stage()): noise depth, EQ gains, comp threshold / ratio / make-up, limiter ceiling.
//   Mute: while on, the output ramps to silence in 5 ms (the screen sends it while the button is held; not automatable).
#pragma once
#include "sw/lr4split.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::lv01 {

enum ParamId { Use, Voice, Mute, kNumParams };
enum UseId { Narration = 0, Stream = 1, Meeting = 2, Singing = 3 };

const std::vector<ParamSpec>& specs();

struct StageValues {
    double noiseDepthDb, lowCutHz, mudDb, presenceDb, airDb, compThreshDb, compRatio, compMakeupDb, ceilingDb;
};

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { if (prepared_) { updateStages(); mute_ = target_[Mute] > 0.5 ? 0.0 : 1.0; } }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    StageValues stage() const;
    double noiseFloorDb(int band) const;

private:
    void updateStages();
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    double depth_ = 0, mud_ = 0, pres_ = 0, air_ = 0, cutHz_ = 20, thr_ = 0, ratio_ = 1, makeup_ = 0;
    Lr4Split3 split_;
    struct Chan { Svf hp, mud, pres, air; double env[3] = {0, 0, 0}, floor[3] = {0.003, 0.003, 0.003}, gain[3] = {1, 1, 1}; };
    std::array<Chan, 2> c_{};
    double cgain_ = 1, lgain_ = 1, env_ = 0, mute_ = 1;
    bool wasBypass_ = true;
    int warm_ = 0;   // samples after start / bypass during which the floor just follows the level
};

}  // namespace sw::lv01
