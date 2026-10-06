// SW RV04 Convolution — convolution reverb (spec: 仕様書 v1.0「RV04 Convolution」). Wet signal only (Mix is the shared frame's); delay 0.
//   No recorded impulse responses are used (the spec says they must be recorded in-house; not done yet): the categories are SYNTHESISED impulse responses
//   (band-limited noise, one decay time per octave band, early reflections), and Custom plays an IR loaded with loadIr() (stored in the project state).
//   in -> Pre-delay -> two sw::TieredConvolver (L and R IR; direct 128 taps + FFT tiers 128 / 1024 / 8192) -> Low cut / High cut.
//   Length (10-100 %, fade-out), Size (50-150 %: the IR is resampled, so it also changes the pitch of the room), Reverse, and Bar fit (EVO: the length snaps to a
//   whole number of beats, 1 / 2 / 4 / 8 / 16 / 32, with a fade) build a new IR. That is done in small steps, one per process call, and crossfaded in over 20 ms.
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include "sw/tiered_convolver.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <vector>

namespace sw::rv04 {

enum ParamId { Category, PreDelay, Length, Size, LowCut, HighCut, Reverse, Mix, BarFit, kNumParams };
enum CategoryId { Halls = 0, Rooms = 1, Churches = 2, Gear = 3, Custom = 4 };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void setTempo(double bpm) { if (std::abs(bpm - bpm_) > 0.05) { bpm_ = bpm; if (target_[BarFit] > 0.5) markTransform(); } }
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    // Custom: an IR from a file (frames x channels interleaved, 1 or 2 channels, any rate); stored with the project
    void loadIr(const float* interleaved, size_t frames, int channels, double sourceRate);
    bool irLoaded() const { return !custom_.empty(); }
    void saveExtra(std::vector<uint8_t>& out) const;
    void loadExtra(const uint8_t* data, size_t size);
    double irSeconds() const { return irLen_ / fs_; }   // length of the IR in use

private:
    enum Stage { Idle, Synth, Transform, Normalise, Load };
    void markSynth() { need_ = std::max(need_, 2); }
    void markTransform() { need_ = std::max(need_, 1); }
    void startJob();
    void beginTransform();
    void stepJob();
    void runAllSync();
    void synthChunk();
    void transformChunk();
    void updateFilters();
    int categoryOf() const { return static_cast<int>(target_[Category] + 0.5); }
    double fs_ = 48000.0, bpm_ = 0.0, preLen_ = 0.0, irLen_ = 0.0;
    size_t maxLen_ = 0, baseLen_ = 0, outLen_ = 0, prePos_ = 0;
    int need_ = 0;           // 0 nothing, 1 transform needed, 2 synthesis needed
    int jobCat_ = 0;
    size_t lt_ = 0, fade_ = 0;
    double sizeRatio_ = 1.0;
    bool rev_ = false, sync_ = false, loaded_ = false;
    int commitIdx_ = 0;
    Stage stage_ = Idle;
    size_t jobPos_ = 0;
    double energy_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<TieredConvolver, 2> conv_{};
    std::array<std::vector<float>, 2> base_, ir_;
    std::array<std::vector<double>, 2> kernel_;
    std::vector<float> custom_;     // interleaved, as loaded
    int customCh_ = 1; double customRate_ = 48000.0;
    std::array<std::vector<float>, 2> pre_;
    std::array<Svf, 2> hp_{}, lp_{};
    // synthesis state
    struct SynthBand { Svf bp; double env = 1.0, step = 1.0; };
    struct SynthCh { std::array<SynthBand, 7> band; uint32_t rng = 1; std::array<double, 48> ax{}, ay{}; };
    std::array<SynthCh, 2> sy_{};
    size_t synthLen_ = 0;
    int synthCat_ = -1;
};

}  // namespace sw::rv04
