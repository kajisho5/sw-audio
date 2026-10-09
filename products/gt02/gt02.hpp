// SW GT02 Cab IR — guitar cabinet and microphone by convolution (spec: 仕様書 v1.0「GT02 Cab IR」)
// No recorded impulse responses are used (the spec says they must be recorded in-house; that is not done yet): the IR is built from a parametric model.
//   magnitude = cabinet (size: low resonance + cone roll-off + presence) x microphone (type) x proximity (distance) x off-axis tilt, made minimum-phase
//   by the real cepstrum, plus an optional decaying room tail (Room). Convolved with one TieredConvolver per channel (latency 0; a direct part, then FFT tiers of 128 / 1024 / 8192: the work does not grow with the square of the IR length at high sample rates). Low cut is a filter after it.
// A change of Cab / Mic / Mic distance / Off axis / Room builds a new IR and crossfades to it over 20 ms.
#pragma once
#include "sw/fft.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include "sw/tiered_convolver.hpp"
#include <array>
#include <complex>
#include <vector>

namespace sw::gt02 {

enum ParamId { Cab, Mic, MicDistance, OffAxis, Room, LowCut, kNumParams };

const std::vector<ParamSpec>& specs();

// the modelled response (linear magnitude, before normalisation) and the finished IR, exposed for tests
double modelMagnitude(double f, int cab, int mic, double distanceCm, double offAxisDeg);
std::vector<double> designIr(int length, double fs, int cab, int mic, double distanceCm, double offAxisDeg, double roomPct);

// the IR builder, in three steps of about a millisecond each so that the audio thread never stalls (all buffers are allocated in prepare)
class IrDesigner {
public:
    void prepare(int fullLength, double fs);   // fullLength: the whole IR (room tail included); the cabinet part is the first quarter
    void begin(int cab, int mic, double distanceCm, double offAxisDeg, double roomPct);
    bool step();                                // true when the IR is ready
    const std::vector<double>& ir() const { return h_; }

private:
    int full_ = 8192, direct_ = 2048, stage_ = 0, cab_ = 2, mic_ = 0;
    double fs_ = 48000.0, dist_ = 4, off_ = 0, room_ = 0, norm_ = 1.0;
    Fft fft_;
    std::vector<std::complex<double>> work_;
    std::vector<double> h_;
};

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }

private:
    void rebuild(bool immediate);   // synchronous (prepare / state load)
    double fs_ = 48000.0;
    int len_ = 8192;
    bool prepared_ = false, dirty_ = false, designing_ = false;
    std::array<double, kNumParams> target_{};
    std::array<TieredConvolver, 2> conv_;   // left and right (the same IR; started half a block apart so that the heavy blocks do not fall into the same call)
    IrDesigner designer_;
    std::array<Svf, 2> hp_{};
    bool lowCutOn_ = true;
};

}  // namespace sw::gt02
