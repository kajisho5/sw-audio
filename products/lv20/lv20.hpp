// SW LV20 Rta — a real-time analyser (spec: 仕様書 v1.0「LV20 Rta」). The sound passes untouched (output = input bit for bit, delay 0, no Auto gain, no Delta).
//   Analysis (the audio thread, fixed buffers): a 16384-point Hann FFT of the mono sum every 4096 samples; bands at 1000 Hz x 2^(k/N) (N = 3, 6, 12 for Resolution 1/3, 1/6, 1/12 octave) from 20 Hz to 20 kHz; the power in a band is the sum of the bins
//   in it (a full-scale sine reads -3.01 dB); a band narrower than two bins (the lowest bands at 1/12 octave) reads the line of the spectrum at its centre. Weight Z (none) / A / C is added in the power domain per band.
//   Speed: the levels are smoothed over Slow 1 s / Medium 300 ms / Fast 100 ms. Peak hold: a band's peak stays for Peak hold seconds (0 = off), then falls 20 dB per second. Freeze stops the numbers where they are.
//   Pink ref (EVO, class A): the line a pink noise would draw in this resolution (equal power in every band) placed at the mean of the measured 100 Hz..10 kHz bands: pinkRefDb() and deviationDb(band) = measured - reference (the room's lean).
#pragma once
#include "sw/fft.hpp"
#include "sw/param.hpp"
#include <array>
#include <complex>
#include <vector>

namespace sw::lv20 {

enum ParamId { Resolution, Speed, PeakHold, Weight, PinkRef, Freeze, kNumParams };
enum ResId { Third = 0, Sixth = 1, Twelfth = 2 };
constexpr int kMaxBands = 12 * 10 + 1;   // 20 Hz .. 20 kHz at 1/12 octave

const std::vector<ParamSpec>& specs();
double weightingDb(int type, double hz);   // 0 Z, 1 A, 2 C

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    int numBands() const { return nb_; }
    double bandHz(int b) const { return centre_[static_cast<size_t>(b)]; }
    double levelDb(int b) const { return shown_[static_cast<size_t>(b)]; }
    double peakDb(int b) const { return peak_[static_cast<size_t>(b)]; }
    double pinkRefDb() const;
    double deviationDb(int b) const { return levelDb(b) - pinkRefDb(); }
    int frames() const { return frames_; }

private:
    void layout();
    void analyse();
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    Fft fft_;
    std::vector<double> win_, ring_;
    std::vector<std::complex<double>> buf_;
    std::vector<double> pow_;   // |X|^2 scaled per bin
    int pos_ = 0, since_ = 0, frames_ = 0, nb_ = 0, layoutRes_ = -1;
    std::array<double, kMaxBands> centre_{}, lo_{}, hi_{}, smooth_{}, shown_{}, peak_{}, hold_{}, wgt_{};
    std::vector<float> mono_;
};

}  // namespace sw::lv20
