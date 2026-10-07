// SW MT02 Spectrum — a spectrum analyser (spec: 仕様書 v1.0「MT02 Spectrum」). The signal passes unchanged; reported delay 0; no Delta / Auto gain.
//   FFT 4k / 8k / 16k / 32k points (Hann, hop N/4) of the mono sum (L + R) / 2, scaled so that a sine of peak amplitude A reads 20 log10(A) dB at its bin (the peak-sine scale; noise reads lower the finer the bins).
//   Speed: the averaging time of the power Slow 2 s / Medium 0.5 s / Fast 0.12 s. Display: Average (the exponential average), Peak (the maximum with a fall of 20 dB/s), Hold (the maximum until reset()).
//   spectrumDb() gives the display values: Smoothing (Off / 1/24 / 1/12 / 1/6 / 1/3 octave, a power average over that width), the Slope tilt (+Slope dB per octave around 1 kHz: pink noise is flat at 3 dB/oct), and the floor
//   (Range: nothing is shown below -Range). EVO (compare A, class B): captureReference() keeps the current average as the reference curve (a long-term average of a reference track, or a genre curve made the same way);
//   compareDb() = the average now - the reference per bin. The genre curves themselves need data (the spec's open point).
#pragma once
#include "sw/fft.hpp"
#include "sw/param.hpp"
#include <array>
#include <complex>
#include <memory>
#include <vector>

namespace sw::mt02 {

enum ParamId { FftSize, Speed, Range, Slope, Smoothing, Display, kNumParams };
enum DisplayId { Peak = 0, Average = 1, Hold = 2 };

const std::vector<ParamSpec>& specs();
constexpr int kMaxFft = 32768;

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    int fftSize() const { return n_; }
    int binCount() const { return n_ / 2 + 1; }
    double binHz(int k) const { return k * fs_ / n_; }
    void spectrumDb(std::vector<float>& out) const;          // binCount() values (display)
    void compareDb(std::vector<float>& out) const;           // average - reference, per bin (0 where there is no reference)
    void captureReference();
    void reset();                                            // clears Hold / Peak / the average

private:
    void frame();
    double fs_ = 48000.0;
    int n_ = 8192, hop_ = 2048, filled_ = 0, since_ = 0, wpos_ = 0, nCur_ = 0;
    bool prepared_ = false, haveRef_ = false;
    std::array<double, kNumParams> target_{};
    std::vector<float> ring_;
    std::vector<std::complex<double>> work_;
    std::vector<double> win_, avg_, peak_, hold_, ref_;
    std::array<std::unique_ptr<Fft>, 4> fft_;
    mutable std::vector<double> tmp_, prefix_;
};

}  // namespace sw::mt02
