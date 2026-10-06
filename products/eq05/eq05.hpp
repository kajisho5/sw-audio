// SW EQ05 Console — 4-band console EQ (spec: 仕様書 v1.0 「EQ05 Console」)
// Signal per channel: [Drive Pre] -> HPF(18 dB/oct) -> LPF(12 dB/oct) -> LF -> LMF -> HMF -> HF -> [Drive Post]
// Output and In are applied by sw::Shell (common frame), after Auto gain as the spec orders.
#pragma once
#include "sw/oversample.hpp"
#include "sw/param.hpp"
#include "sw/saturate.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::eq05 {

enum ParamId {
    HfGain, HfFreq, HfShape, HmfGain, HmfFreq, HmfQ, LmfGain, LmfFreq, LmfQ,
    LfGain, LfFreq, LfShape, Hpf, Lpf, Drive, DrivePos, Output, In, kNumParams
};

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);       // takes effect smoothly
    double getParam(int id) const { return target_[static_cast<size_t>(id)]; }
    void snapToTargets();                           // jump to targets (after state load)
    void process(float** ch, int numCh, int n);     // in place, 1 or 2 channels
    int latencySamples() const { return 0; }

private:
    struct Chain {
        Svf hfShelf, hfBell, hmf, lmf, lfShelf, lfBell, hpf2, lpf;
        OnePole hpf1;
        Oversampler2x os;
    };
    struct Ctl {  // smoothed control values shared by both channels
        LinearSmoother hfGain, hfFreqN, hmfGain, hmfFreqN, hmfQN, lmfGain, lmfFreqN, lmfQN,
            lfGain, lfFreqN, hpfFreqN, lpfFreqN, drive;
        LinearSmoother hfBell, lfBell, hpfOn, lpfOn;  // 10 ms crossfades
    };
    void updateCoefficients(int rampSamples = 0);
    double eq(Chain& c, double x) const;
    double drive(Chain& c, double x) const;
    double runChain(Chain& c, double x, int pos) const;
    bool anySmoothing() const;

    double fs_ = 48000.0;
    std::array<double, kNumParams> target_{};
    Ctl ctl_;
    std::array<Chain, 2> ch_{}, fade_{};
    Saturator sat_;
    int drivePos_ = 1, fadePos_ = 1, fadeRemaining_ = 0, fadeLength_ = 480;
    double hfBell_ = 0, lfBell_ = 0, hpfOn_ = 0, lpfOn_ = 0;
};

}  // namespace sw::eq05
