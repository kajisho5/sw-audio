// SW EQ08 Linear — 24-band EQ: Linear (FIR from the combined magnitude, partitioned FFT convolution),
// Minimum (TPT SVF IIR, zero latency), Mixed (below 200 Hz minimum phase, above linear). spec: 仕様書 v1.0「EQ08 Linear」
// Kernel: 2048 taps at 48 kHz (scaled with fs), latency = L/2 + one 128-sample block. New kernels crossfade over 20 ms.
// EVO Pre-ring guard: bands whose own linear kernel rings more than 3 ms ahead of the peak above -60 dB go minimum phase.
#pragma once
#include "sw/convolver.hpp"
#include "sw/fir_design.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::eq08 {

constexpr int kBands = 24;
enum BandField { On, Type, Freq, Gain, Q, kPerBand };
enum ParamId { Phase = kBands * kPerBand, Guard, Ms, Output, Length, kNumParams };  // Length appended (ids stay stable)
enum PhaseMode { Linear, Minimum, Mixed };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const;  // desired (Phase); applied at prepare
    int kernelLength() const { return L_; }

private:
    BandShape shape(int b) const;
    bool active(int b) const { return target_[static_cast<size_t>(b * kPerBand + On)] > 0.5; }
    bool guarded(int b);
    void rebuildKernel(bool immediate);
    void updateIir(int ramp);
    static int kernelLengthFor(double fs, double base);
    double fs_ = 48000.0;
    int L_ = 2048, B_ = 128, mode_ = Linear, sinceKernel_ = 0, fadeLen_ = 960, dpos_ = 0;
    bool dirty_ = false, iirDirty_ = false, prepared_ = false;
    std::array<double, kNumParams> target_{};
    Convolver conv_;
    std::array<std::array<Svf, kBands>, 2> iir_{};
    std::vector<float> sideDelay_;
    struct GuardCache { double key[4] = {-1, -1, -1, -1}; bool guard = false; };
    std::array<GuardCache, kBands> guardCache_{};
    std::vector<float> scratch_;
};

}  // namespace sw::eq08
