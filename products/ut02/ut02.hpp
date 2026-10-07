// SW UT02 Mono Check — monitoring tool (spec: 仕様書 v1.0「UT02 Mono Check」). Reported delay 0; no Auto gain, no Delta. MONITORING ONLY: exportWarning() is true whenever the output is not the plain stereo signal
//   (Listen not Stereo, Phone speaker on, Low cut on) so that the screen can warn before a bounce.
//   Listen: Stereo (untouched), Mono ((L + R) x the Mono fold gain in both channels; fold -6 .. 0 dB, default -3: the equal-power sum, 0 dB for uncorrelated material, +3 dB for a mono one),
//   Side ((L - R) / 2 in both), Left, Right (that channel in both). Phone speaker (EVO, class A): the small-speaker model of LO01 (a 4th-order high-pass at 300 Hz and a +3 dB bell at 1.2 kHz, Q 2.5).
//   Low cut: Off, 20 .. 300 Hz (a second-order high-pass; the lowest position is Off). Level -24 .. +24 dB after everything (not in plain Stereo with the rest off: it is a gain like any other).
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::ut02 {

enum ParamId { Listen, MonoFold, Phone, LowCut, Level, kNumParams };
enum ListenId { Stereo = 0, Mono = 1, Side = 2, Left = 3, Right = 4 };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    bool exportWarning() const { return target_[Listen] > 0.5 || target_[Phone] > 0.5 || target_[LowCut] > 20.5; }

private:
    void setFilters();
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    struct Chan { Svf hp1, hp2, bell, low; };
    std::array<Chan, 2> ch_{};
};

}  // namespace sw::ut02
