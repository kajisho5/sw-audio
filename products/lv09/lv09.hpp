// SW LV09 Hum Cut — the LIVE version of RS03 Dehum (spec: 仕様書 v1.0「LV09 Hum Cut」: "RS03 と同じ処理の LIVE 版"). Reported delay 0. The notch comb and the drift tracker are sw-rs03's.
//   Base 50 / 60 / Auto -> RS03 Base. Harmonics 1 .. 16 -> exactly that many notches (RS03 itself takes 2 / 4 / 8 / 16). Depth 0 .. -40 dB -> RS03 Depth (4 dB a step, continuous here).
//   Width Narrow / Medium / Wide -> RS03 Width 0 / 50 / 100 %. Track drift -> RS03 Track. Buzz is not offered (0).
//   Listen: what the comb takes away (input - output) instead of the signal (not automatable).
#pragma once
#include "rs03/rs03.hpp"
#include "sw/param.hpp"
#include <array>
#include <vector>

namespace sw::lv09 {

enum ParamId { Base, Harmonics, Depth, Width, TrackDrift, Listen, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { core_.snapToTargets(); }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double humHz() const { return core_.humHz(); }

private:
    void apply(int id);
    rs03::Processor core_;
    std::array<double, kNumParams> target_{};
    std::array<std::vector<float>, 2> dry_;
    bool prepared_ = false;
};

}  // namespace sw::lv09
