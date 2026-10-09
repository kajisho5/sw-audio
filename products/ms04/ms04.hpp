// SW MS04 Clipper — oversampled clipper whose knee morphs from hard clip to tape-like (spec: 仕様書 v1.0「MS04 Clipper」)
// Linear-phase FIR oversampling 4x/8x/16x (latency 48 samples for every factor). Mix is applied by sw::Shell.
//   Low lat (the last parameter, spec: common function): the oversampler becomes a cascade of minimum-phase IIR half-bands (sw::IirOversampler): a short reported delay (8 samples: the half-bands' low-frequency group delay; the spec says 0, but then Mix would comb), a little overshoot / phase shift instead of the FIR's pre-ringing.
//   Like every setting that changes the delay it applies at the next prepare(); latencySamples() already gives the value for it.
#pragma once
#include "sw/oversample.hpp"
#include "sw/oversample_fir.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include <array>
#include <vector>

namespace sw::ms04 {

enum ParamId { Drive, Ceiling, Knee, Mix, Oversample, GainMatch, Listen, LowLat, kNumParams };

const std::vector<ParamSpec>& specs();

// normalised transfer curve (ceiling = 1). knee01: 0 hard, 0.5 soft (quadratic knee), 1 tape-like (tanh)
double clipCurve(double u, double knee01);

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    static constexpr int kIirDelay = 8;   // Low lat: the IIR half-bands delay low frequencies by 7.2 / 8.4 / 9.0 samples at 4x / 8x / 16x (measured, 48 kHz): reported, so that the dry path of Mix is aligned
    int latencySamples() const { return target_[LowLat] > 0.5 ? kIirDelay : FirOversampler::kTapsPerPhase; }   // for the next prepare

private:
    static int factorIndex(double f) { return f < 6 ? 0 : (f < 12 ? 1 : 2); }
    template <class Os> double runOne(Os& os, double x, double ceil, double knee);
    double fs_ = 48000.0;
    std::array<double, kNumParams> target_{};
    std::array<std::array<FirOversampler, 3>, 2> os_{};
    std::array<std::array<IirOversampler, 3>, 2> osIir_{};   // Low lat
    std::array<std::array<IirOversampler, 3>, 2> osDry_{};   // Low lat + Listen: the same filters without the clipper, so that what is taken away is computed from an aligned dry signal (an IIR has no single delay)
    bool iir_ = false;
    LinearSmoother drive_, ceil_, knee_;
    int active_ = 1, previous_ = 1, fade_ = 0, fadeLen_ = 480;
    std::vector<double> buf_;
    std::array<std::vector<double>, 2> dly_{};
    int dpos_ = 0;
};

}  // namespace sw::ms04
