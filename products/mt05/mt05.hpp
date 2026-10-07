// SW MT05 Vu Ppm — a VU / PPM meter (spec: 仕様書 v1.0「MT05 Vu Ppm」). The signal passes unchanged; reported delay 0; no Delta / Auto gain / Unit (the spec recommends dropping them).
//   Ref dBFS (-14 / -18 / -20): the level of a sine (RMS) that reads 0 VU, or 0 on the PPM scale. VU: the rectified average x 1.1107 (so that a sine reads its RMS), through a one-pole of 65 ms (99 % of a step in 300 ms),
//   in dB re Ref. PPM (design: the quasi-peak of IEC 60268-10 type II): the rectified signal rises with a time constant of 4.5 ms (a 10 ms burst reaches about 1 dB under the steady value) and falls by
//   20 dB in 1.7 s linearly in dB. levelDb(ch) is the reading of the chosen Meter. EVO (class A, the spec): the 0 VU reference as a project setting shared by several MT05 over SW Link needs SW Link: not yet
//   (Ref is a per-instance parameter).
#pragma once
#include "sw/param.hpp"
#include <array>
#include <vector>

namespace sw::mt05 {

enum ParamId { RefDb, MeterType, kNumParams };
enum MeterId { Vu = 0, Ppm = 1 };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double levelDb(int channel) const;       // the reading of the chosen Meter, dB re Ref (0 = 0 VU / PPM reference)
    double vuDb(int channel) const;
    double ppmDb(int channel) const;
    void reset();

private:
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<double, 2> vu_{}, ppm_{};
};

}  // namespace sw::mt05
