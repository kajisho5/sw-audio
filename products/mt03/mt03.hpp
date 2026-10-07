// SW MT03 Spectrogram — a scrolling spectrogram (spec: 仕様書 v1.0「MT03 Spectrogram」). The signal passes unchanged (except while the band-pass preview is on); reported delay 0; no Delta / Auto gain.
//   STFT of the mono sum: 4096 points (Hann), hop 1024 (21.3 ms at 48 kHz); every frame becomes a column of 256 display bands on the chosen Scale (Linear 20 Hz .. 20 kHz evenly, Log evenly in octaves,
//   Mel evenly in mel) in dB (the peak-sine scale of MT02: a sine of peak A reads 20 log10 A), kept for Scroll seconds (2 .. 60). Floor (-120 .. -60 dB) is the lowest value. Contrast, Palette, Show notes and Show freq
//   are the screen's (stored). EVO (class A): setPreview(true, lo, hi) listens to the band (a 4th-order band-pass with the loss at its centre made up (at most +12 dB), 20 ms cross-fade) - while it is on the OUTPUT IS CHANGED (previewActive() is the screen's
//   warning that nothing should be bounced meanwhile). The Heat palette must not use the state colours (the screen's rule).
#pragma once
#include "sw/fft.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <complex>
#include <vector>

namespace sw::mt03 {

enum ParamId { Scale, Scroll, Floor, Contrast, Palette, ShowNotes, ShowFreq, kNumParams };
enum ScaleId { Linear = 0, Log = 1, Mel = 2 };

const std::vector<ParamSpec>& specs();
constexpr int kBands = 256, kFft = 4096, kHop = 1024, kMaxColumns = 3000;
double bandHz(int scale, int band);          // the centre of a display band (band 0 .. kBands - 1)
int bandOf(int scale, double hz);            // the band that holds a frequency

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    int columnCount() const { return count_; }                                  // at most Scroll / hop time
    void column(int index, std::vector<float>& out) const;                      // 0 = the oldest kept
    void setPreview(bool on, double loHz, double hiHz);
    bool previewActive() const { return pvOn_ || pvMix_ > 0.0; }

private:
    void frame();
    double fs_ = 48000.0, pvMix_ = 0.0, pvGain_ = 1.0;
    int since_ = 0, wpos_ = 0, filled_ = 0, head_ = 0, count_ = 0, maxCols_ = 470;
    bool prepared_ = false, pvOn_ = false;
    double pvLo_ = 1000, pvHi_ = 2000;
    std::array<double, kNumParams> target_{};
    Fft fft_;
    std::vector<float> ring_;
    std::vector<std::complex<double>> work_;
    std::vector<double> win_;
    std::vector<float> cols_;   // kMaxColumns x kBands, a ring
    std::array<std::array<Svf, 2>, 2> hp_{}, lp_{};
};

}  // namespace sw::mt03
