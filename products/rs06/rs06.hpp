// SW RS06 Dereverb — statistical suppression of the late reverberation (spec: 仕様書 v1.0「RS06 Dereverb」). STFT 1024 points, hop 256 (sw/stft.hpp), reported delay 1024 samples.
//   Per bin: the smoothed power Ps (0.6 per frame); the late reverberation power  lambda(l) = max(rho lambda(l-1), rho^D Ps(l-D)),  rho = 10^(-6 h / Tail) (the power falls 60 dB in Tail seconds,
//   h = 5.33 ms the frame step) and D the frames of the early part (Early Keep 50 ms = 9 frames, Reduce 20 ms = 4): what arrives later than D behind a louder moment is reverberation.
//   Gain = xi / (1 + xi), xi = max(P / lambda - 1, 0), floored at Reduction (0 .. -30 dB); Smooth (Low / Mid / High) = release 0.5 / 0.7 / 0.85 per frame (the gain opens at once), Mid and High also a 3-bin smoothing.
//   Learn room (EVO): while it is On, the broadband level is watched for decays (a fall of 12 dB or more over 32 frames, no rise above 1 dB in a step): -60 / slope is a candidate for Tail;
//   when Learn goes Off the median of the candidates is written to Tail (takeParamWrite, 0.1 .. 5 s). No candidate: Tail stays.
#pragma once
#include "sw/param.hpp"
#include "sw/stft.hpp"
#include <array>
#include <complex>
#include <vector>

namespace sw::rs06 {

enum ParamId { Reduction, Tail, Early, Smooth, Learn, kNumParams };
enum EarlyId { Keep = 0, ReduceEarly = 1 };

const std::vector<ParamSpec>& specs();
constexpr int kFft = 1024, kHop = 256, kHist = 64;

class Processor : public Stft::Handler {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return kFft; }
    void frame(std::complex<double>* const* spec, int nch, int nbins) override;
    // plugin layer: bit 0 begin, bit 1 value (plain), bit 2 end; the Tail value found by Learn room
    int takeParamWrite(int& id, double& plain);

private:
    struct Chan { std::vector<double> ps, lam, gs; std::vector<std::vector<double>> hist; int pos = 0; };
    void learnFrame(double dB);
    double fs_ = 48000.0;
    bool prepared_ = false, learning_ = false;
    int frameNo_ = 0;
    std::array<double, kNumParams> target_{};
    Stft stft_;
    std::array<Chan, 2> ch_;
    std::vector<double> g_, tmp_, levels_, cand_;
    int lpos_ = 0, lcount_ = 0, pending_ = 0;
    double found_ = 0.0;
};

}  // namespace sw::rs06
