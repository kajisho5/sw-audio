// SW EQ02 Surgical — 24-band surgical EQ (spec: 仕様書 v1.0「EQ02 Surgical 進化版」)
// Zero latency: TPT SVF with bandwidth pre-warp against cramping; cuts 6..96 dB/oct as Butterworth cascades.
// Natural: Zero latency + a 65-tap phase-only FIR that moves the phase toward the analog prototype (latency 32).
// Linear: FIR from the target magnitude on the shared engine (same as EQ08), latency L/2 + 128.
// Per band: dynamic range / threshold (band-limited detector), Stereo / Mid / Side placement when Mid/side is on.
// Assist / Unmask (analysis, EVO B) come with the UI.
#pragma once
#include "sw/bandfilter.hpp"
#include "sw/convolver.hpp"
#include "sw/fir_design.hpp"
#include "sw/param.hpp"
#include <array>
#include <vector>

namespace sw::eq02 {

constexpr int kBands = 24;
enum BandField { On, Type, Freq, Gain, Q, Slope, Place, DynRange, DynThresh, kPerBand };
enum ParamId { PhaseMode = kBands * kPerBand, Ms, Output, Length, kNumParams };  // Length appended (ids stay stable)
enum Phase { ZeroLatency, Natural, Linear };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const;

private:
    static constexpr int kNatDelay = 32, kNatTaps = 2 * kNatDelay + 1, kControl = 16;
    double t(int b, int f) const { return target_[static_cast<size_t>(b * kPerBand + f)]; }
    bool active(int b) const { return t(b, On) > 0.5; }
    bool onPath(int b, int p) const;
    bool dynamic(int b) const;
    BandShape shape(int b, double gainOffset = 0.0) const;
    void configure(int ramp);
    void buildNatural();
    void buildLinear(bool immediate);
    static int kernelLengthFor(double fs, double base);
    double fs_ = 48000.0;
    int mode_ = ZeroLatency, L_ = 2048, sinceKernel_ = 0, fadeLen_ = 960, natPos_ = 0;
    bool prepared_ = false, dirty_ = true, kernelDirty_ = true;
    std::array<double, kNumParams> target_{};
    struct Band {
        std::array<BandFilter, 2> f{};      // per path: the band (Zero latency / Natural) or its dynamic part (Linear)
        std::array<Svf, 2> det{};           // band-limited detector per path
        std::array<double, 2> env{}, offset{};
    };
    std::array<Band, kBands> band_{};
    std::array<Convolver, 2> conv_{};
    std::array<std::vector<double>, 2> nat_{}, natHist_{};
    double atk_ = 0, rel_ = 0;
};

}  // namespace sw::eq02
