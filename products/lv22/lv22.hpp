// SW LV22 Polarity — a polarity checker (spec: 仕様書 v1.0「LV22 Polarity」). The sound passes untouched (bit for bit, delay 0, no Auto gain, no Delta).
//   Test signal against a reference: with a sidechain connected the input is the test (a mic) and the sidechain the reference (the LV21 Polarity pulse or any signal sent to the speaker); without one, the left channel is the test and the right the reference
//   (Mic vs mic: two mics on one source). Both are brought to about 12 kHz (box average) and the cross-correlation over the last Window ms is taken by FFT; the lag with the largest |r| inside the search range
//   (Mic vs mic +-5 ms, Speaker +-60 ms, Line +-1 ms) decides: r > 0 In phase, r < 0 Out of phase, and the normalised |r| must be at least 0.3 and the energies not tiny, else Unknown. lagMs() is that lag (the test behind the reference when positive).
//   The result is updated every max(100 ms, Window / 2). Hold result On: an Unknown does not replace an earlier decisive result (it stays until the next decisive one); Off: Unknown is shown as it is.
#pragma once
#include "sw/fft.hpp"
#include "sw/param.hpp"
#include <array>
#include <complex>
#include <vector>

namespace sw::lv22 {

enum ParamId { Mode, Window, HoldResult, kNumParams };
enum ModeId { MicVsMic = 0, Speaker = 1, Line = 2 };
enum Result { Unknown = 0, InPhase = 1, OutOfPhase = 2 };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n) { processWithSidechain(ch, numCh, n, nullptr, 0); }
    void processWithSidechain(float** ch, int numCh, int n, const float* const* sc, int scCh);
    int latencySamples() const { return 0; }
    int result() const { return result_; }
    double correlation() const { return corr_; }
    double lagMs() const { return lagMs_; }

private:
    void analyse();
    static constexpr int kN = 16384;   // FFT size at the decimated rate (about 12 kHz): window 1 s + the lag range fit
    double fs_ = 48000.0, fd_ = 12000.0, accA_ = 0, accB_ = 0, corr_ = 0, lagMs_ = 0;
    int dec_ = 4, decN_ = 0, ring_ = 0, pos_ = 0, since_ = 0, result_ = Unknown;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    Fft fft_;
    std::vector<float> a_, b_;
    std::vector<std::complex<double>> fa_, fb_;
};

}  // namespace sw::lv22
