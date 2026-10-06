// SW ST03 Phase Align — time and phase alignment of two microphones (spec: 仕様書 v1.0「ST03 Phase Align」). Wet signal only (Mix is the shared frame's, default 100 %); reported delay 0.
//   The signal is delayed (Delay 0 .. 20 ms, rounded to 0.01 ms, 4-point Hermite), rotated in phase (Phase -180 .. +180 deg: a constant rotation over the whole band, made with the IIR Hilbert pair:
//   out = cos(phi) i + sin(phi) q; exact bypass below 0.05 deg; +-180 is an inversion) and optionally inverted (Polarity). Insert it on the EARLIER track: it can only delay.
//   The reference microphone comes in on the external sidechain (processWithSidechain); it is never added to the output.
//   Auto align (EVO, class B): startAutoAlign() collects 4 s of the track and of the reference (mono sums) in the audio thread (state Collecting -> Ready); analyse() (not real time: the screen calls it
//   from the main thread) then finds the lag in 0 .. 20 ms with the largest |cross-correlation| (full-band FFT correlation, refined at the sample), and with that lag compares three candidates on the LOW band (below 250 Hz): the track as it is (Phase 0, bypass: C0 = sum(x y)),
//   its inversion (-C0) and the rotator: with i, q of the delayed track and the reference y, A = sum(i y), B = sum(q y), phi = atan2(B, A), best value sqrt(A^2 + B^2) (closed form, no search).
//   The largest wins. Note the rotator's phase is that of the Hilbert pair's output (which carries its own all-pass phase), so a plain delayed copy gets Phase 0, never a small odd angle. takeParamWrite() hands the three
//   values (Delay, Phase, Polarity) to the host as gestures. Failure (silent reference, normalised correlation < 0.1) leaves the parameters alone (state Failed).
#pragma once
#include "sw/hilbert.hpp"
#include "sw/param.hpp"
#include <array>
#include <vector>

namespace sw::st03 {

enum ParamId { Delay, Phase, Polarity, Mix, kNumParams };
enum AlignState { Idle = 0, Collecting = 1, Ready = 2, Done = 3, Failed = 4 };

const std::vector<ParamSpec>& specs();
constexpr double kMaxDelayMs = 20.0, kCollectSeconds = 4.0, kLowBandHz = 250.0, kMinConfidence = 0.1;

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n) { processWithSidechain(ch, numCh, n, nullptr, 0); }
    void processWithSidechain(float** ch, int numCh, int n, const float* const* sc, int scCh);
    int latencySamples() const { return 0; }
    // Auto align
    void startAutoAlign();
    int alignState() const { return state_; }
    bool analyse();                        // Ready -> Done / Failed
    double confidence() const { return confidence_; }
    double foundDelayMs() const { return foundMs_; }
    double foundPhaseDeg() const { return foundDeg_; }
    bool foundInvert() const { return foundInvert_; }
    int takeParamWrite(int& id, double& plain);   // bit 0 begin, 1 value, 2 end (plugin layer)

private:
    double read(int c, double delay) const;
    double fs_ = 48000.0, delay_ = 0.0, phaseT_ = 0.0, cosT_ = 1.0, sinT_ = 0.0, rotMix_ = 0.0;
    size_t pos_ = 0, mask_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<std::vector<float>, 2> buf_;
    std::array<HilbertIir, 2> hil_{};
    // Auto align
    int state_ = Idle; size_t collected_ = 0;
    std::vector<float> capMain_, capRef_;
    double confidence_ = 0.0, foundMs_ = 0.0, foundDeg_ = 0.0; bool foundInvert_ = false;
    int pending_ = 0;   // parameter writes still to hand out: 3 = Delay, 2 = Phase, 1 = Polarity
};

}  // namespace sw::st03
