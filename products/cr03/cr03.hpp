// SW CR03 Granular — grains cut from the last seconds of the input (spec: 仕様書 v1.0「CR03 Granular」). Wet signal only (Mix is the shared frame's); reported delay 0 (grains read the past).
//   Grains: Hann-windowed pieces of the input, Grain ms long, born Density times a second (the gap is 1/density x 0.7 .. 1.3), at most 64 at once, each played at the rate 2^(Pitch/12), normalised
//   by 1/sqrt(overlap) (overlap = density x length). Mode: Cloud = the grain starts one grain length (x the rate) back, Spray % adds up to 0.5 s more at random; Scatter = anywhere in the last 2 s
//   (Spray widens ... the span is always the whole 2 s) with a random direction of nothing but a random pan; Glitch = starts and lengths locked to a grid of the grain length (1 .. 4 grains back), a quarter of
//   the grains reversed, a third repeating the previous start (rhythmic repeats). Spread: Mono (centre) / Narrow (+-0.3) / Wide (+-1) random pan, constant power. Freeze input stops writing: the grains keep
//   reading the frozen last 2 seconds (or 4 s).
//   Harmony (EVO, cr03.evo.on, On): every 0.125 s the pitch-class distribution of the input is measured (8192-point FFT of the last 0.17 s, spectral peaks 60 Hz .. 2 kHz, mapped to the 12 notes) and kept for
//   the last 4 s: its strongest classes (at least half of the maximum, at most 4) are the chord. A grain born from the frame whose strongest class is s gets, instead of Pitch, the shift k nearest to Pitch
//   (within +-6 semitones, chosen at random among the two best) for which s + k is a chord tone. No clear chord (energy below -60 dB): Pitch.
#pragma once
#include "sw/fft.hpp"
#include "sw/param.hpp"
#include <array>
#include <complex>
#include <cstdint>
#include <vector>

namespace sw::cr03 {

enum ParamId { Mode, Grain, Density, Spray, Pitch, Spread, Mix, Freeze, Harmony, kNumParams };
enum ModeId { Cloud = 0, Scatter = 1, Glitch = 2 };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    double tailSeconds() const;   // how long it goes on after the input stops (sw/tail.hpp)
    int latencySamples() const { return 0; }
    int activeGrains() const { return active_; }
    // chord of the last 4 s: bit i = pitch class i (0 = C)
    int chordMask() const { return chord_; }
    // for the screen (audio thread): the grains playing now, up to kScopeGrains, kGrainValues each: how far back in the input they read (seconds), speed (rate; negative = reversed),
    // place in the window (0 .. 1), source span (seconds, length x |rate|), pan (-1 .. +1). Returns how many were written.
    static constexpr int kScopeGrains = 24, kGrainValues = 5;
    int grains(double* out) const;
    double sampleRate() const { return fs_; }

private:
    struct GrainState { bool on = false; double pos = 0, step = 1, g = 1; int age = 0, len = 1; float l = 0.7f, r = 0.7f; };
    void spawn();
    void analyseChroma();
    double rnd();
    double fs_ = 48000.0, nextIn_ = 0.0, lastStart_ = 0.0, lastLen_ = 0.0;
    int64_t t_ = 0;
    int active_ = 0, chord_ = 0, sinceChroma_ = 0, frameCount_ = 0, framePos_ = 0;
    bool prepared_ = false;
    uint32_t rng_ = 0x2545F491u;
    size_t wpos_ = 0, mask_ = 0;
    std::array<double, kNumParams> target_{};
    std::array<std::vector<float>, 2> buf_;
    std::array<GrainState, 64> grains_{};
    Fft fft_;
    std::vector<std::complex<double>> work_;
    std::vector<double> win_, mag_;
    static constexpr int kFrames = 32;
    std::array<std::array<double, 12>, kFrames> chroma_{};
    std::array<int, kFrames> frameStrongest_{};
    std::array<int64_t, kFrames> frameTime_{};
    std::array<bool, kFrames> frameValid_{};
};

}  // namespace sw::cr03
