// SW RS01 Denoise — spectral noise suppression (spec: 仕様書 v1.0「RS01 Denoise」). 2048-point STFT, hop 512, sqrt-Hann pair; reported delay 2048 samples.
//   Low lat (the spec's common function, here the last parameter): 512-point STFT, hop 128, reported delay 512. Like every setting that changes the delay it takes effect at the next prepare()
//   (the host restarts the plug-in); latencySamples() already gives the value for it.
//   Noise estimate per bin: Adaptive On = minimum statistics (the smoothed power's minimum over 8 sub-windows of 0.2 / 0.4 / 0.25 s (Voice / Music / Field), x1.5 bias correction);
//   Adaptive Off = frozen (Learn first); Learn On = the mean power while it is on replaces the estimate. Gain: decision-directed a-priori SNR (alpha 0.93 / 0.96 / 0.98 for Smoothing Low / Mid / High)
//   over lambda x 10^(Threshold/10), Wiener gain xi/(1+xi), floored at Reduction (0 .. -40 dB) plus the band correction: Low band (below 400 Hz, gone by 1.2 kHz) and High band (above 4 kHz,
//   gone by 1.5 kHz) add their dB to the depth (+ = deeper, - = shallower). Artifact guard On: the gain follows a fast attack / slow release in time and a 3-bin smoothing in frequency (musical noise).
//   Profile (design values): Voice / Music / Field = minimum-statistics window 1.6 s / 3.2 s / 2 s, Field a 1.15 over-subtraction.
#pragma once
#include "sw/param.hpp"
#include "sw/stft.hpp"
#include <array>
#include <complex>
#include <vector>

namespace sw::rs01 {

enum ParamId { Profile, Adaptive, Reduction, Threshold, Smoothing, LowBand, HighBand, Guard, Learn, LowLat, kNumParams };

const std::vector<ParamSpec>& specs();
constexpr int kFft = 2048, kHop = 512, kFftLow = 512, kHopLow = 128;   // the window of the normal and the Low lat mode

class Processor : public Stft::Handler {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return target_[LowLat] > 0.5 ? kFftLow : kFft; }   // for the next prepare
    void frame(std::complex<double>* const* spec, int nch, int nbins) override;
    double noiseDb(int bin) const;   // the noise estimate of a bin (dB, mean square per bin)

private:
    struct Chan {
        std::vector<double> smooth, sub, noise, learnSum, gainPrev, prevSig, gainSmooth;
        std::vector<std::vector<double>> mins;
        int subPos = 0, subCount = 0, learnN = 0;
    };
    double fs_ = 48000.0;
    bool prepared_ = false;
    int frameNo_ = 0, fft_ = kFft, hop_ = kHop;   // the window in use (set by prepare)
    std::array<double, kNumParams> target_{};
    Stft stft_;
    std::array<Chan, 2> ch_;
    std::vector<double> bandAdj_, tmpG_;
};

}  // namespace sw::rs01
