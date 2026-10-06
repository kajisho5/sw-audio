// SW EQ07 Dynamic — 6-band dynamic EQ (spec: 仕様書 v1.0「EQ07 Dynamic」)
// Band: TPT SVF. Detector: band-pass at the band's frequency / Q (or the external sidechain), program detection,
// 6 dB soft knee. Effective gain = Gain + Range x amount. Spectral: STFT (1024 / hop 256) moves only the bins that
// stick out inside the band (latency 1024). Auto thresh (learn button, EVO B) comes with the UI.
#pragma once
#include "sw/bandfilter.hpp"
#include "sw/dynamics.hpp"
#include "sw/fft.hpp"
#include "sw/param.hpp"
#include <array>
#include <complex>
#include <vector>

namespace sw::eq07 {

constexpr int kBands = 6;
enum BandField { On, Type, Freq, Gain, Q, Thresh, Range, Attack, Release, kPerBand };
enum ParamId { Sidechain = kBands * kPerBand, Spectral, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n) { run(ch, numCh, n, nullptr, 0); }
    void processWithSidechain(float** ch, int numCh, int n, const float* const* sc, int scCh) { run(ch, numCh, n, sc, scCh); }
    int latencySamples() const { return target_[Spectral] > 0.5 ? kN : 0; }  // desired; applied at prepare

private:
    static constexpr int kN = 1024, kHop = 256, kControl = 16;
    double t(int b, int f) const { return target_[static_cast<size_t>(b * kPerBand + f)]; }
    bool active(int b) const { return t(b, On) > 0.5; }
    bool hasGain(int b) const { const int ty = static_cast<int>(t(b, Type)); return ty == 0 || ty == 1; }  // Bell / Shelf
    BandShape shape(int b, double gainDb) const;
    void configure(int ramp);
    void run(float** ch, int numCh, int n, const float* const* sc, int scCh);
    void spectralFrame(int c);
    double fs_ = 48000.0;
    bool spectral_ = false, prepared_ = false;
    std::array<double, kNumParams> target_{};
    struct Band {
        std::array<BandFilter, 2> f{};
        std::array<Svf, 2> det{};
        std::array<LevelDetector, 2> lvl{};
        double amount = 0, applied = 0;
    };
    std::array<Band, kBands> band_{};
    // spectral
    Fft fft_{kN};
    std::vector<double> win_;
    struct Stft { std::vector<double> in, ola, levelDb; int w = 0, r = 0, hop = 0; };
    std::array<Stft, 2> st_{};
    std::vector<std::complex<double>> buf_;
    std::vector<double> gainDb_;
};

}  // namespace sw::eq07
