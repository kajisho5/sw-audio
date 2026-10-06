// SW ST01 Imager — four-band stereo width (spec: 仕様書 v1.0「ST01 Imager」). The whole signal is processed (no Mix); reported delay 0.
//   Each channel is split by a 4th-order Linkwitz-Riley four-band split (sw::Lr4Split4, crossovers 200 Hz / 2 kHz / 8 kHz, kept an octave apart); per band M = (L + R) / 2, S = (L - R) / 2,
//   S x width (0 .. 200 %: 0 = mono, 100 = untouched, 200 = twice the side) and L = M + S, R = M - S; the bands add up again (flat in magnitude).
//   Mono check (monitoring, not automatable): both outputs are (L + R) / 2. EVO: per band correlation of the OUTPUT (L against R, 300 ms mean) and a mark where it is below 0
//   (a band pushed so wide that it cancels in mono): correlation(band), overWide(band) for the screen.
#pragma once
#include "sw/lr4split.hpp"
#include "sw/param.hpp"
#include <array>
#include <vector>

namespace sw::st01 {

enum ParamId { Low, LoMid, HiMid, High, Xover1, Xover2, Xover3, MonoCheck, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double correlation(int band) const;        // -1 .. 1 (1 when there is no signal)
    bool overWide(int band) const { return correlation(band) < 0.0 && energy_[static_cast<size_t>(band)] > 1e-9; }

private:
    void setSplit();
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    Lr4Split4 split_;
    double splitF_[3] = {0, 0, 0};
    std::array<double, 4> sLR_{}, sLL_{}, sRR_{}, energy_{};
};

}  // namespace sw::st01
