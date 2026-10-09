// SW CR01 Filter — a state-variable filter driven by the signal's envelope, an LFO or the external sidechain (spec: 仕様書 v1.0「CR01 Filter」). No Mix; reported delay 0.
//   Two-pole zero-delay-feedback state-variable filter (LP / BP / HP / Notch) at 2x oversampling, with a tanh in the input and in the band-pass state, both blended in by Drive (Drive 0 is exactly linear).
//   Resonance 0 .. 100 %: damping k = 2 (1 - 0.95 r), at least 0.1 (the peak at the cutoff is 1/k: up to +20 dB). Drive 0 .. 100 %: input gain G = 1 + 9 d, x + d (tanh(G x) / G - x) (gain 1 for small signals); the band-pass state x + d (tanh x - x).
//   Mod source: Envelope (the input), LFO (one cycle per bar from the host tempo, 0.5 Hz without one), Sidechain (the external input; without one the input itself). The modulator m (0 .. 1) moves
//   the cutoff by 2^(5 x Env amount x m) (+-100 % = +-5 octaves). The envelope: peak follower, attack 3 ms, release 120 ms, in dB.
//   EVO (appended parameter cr01.evo.on, On): m = (level - low) / (high - low) of the last 10 seconds' range (the highest and the lowest level of 20 blocks of 0.5 s, silence below -70 dB left out; at least
//   6 dB apart): whatever the level, Env amount works over the whole range. Off: m = (level + 40 dB) / 40 dB.
#pragma once
#include "sw/oversample.hpp"
#include "sw/param.hpp"
#include <array>
#include <vector>

namespace sw::cr01 {

enum ParamId { Type, ModSource, Cutoff, Resonance, EnvAmount, Drive, Evo, Oversample, kNumParams };
enum TypeId { LP = 0, BP = 1, HP = 2, Notch = 3 };
enum SourceId { Envelope = 0, Lfo = 1, Sidechain = 2 };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void setTempo(double bpm) { bpm_ = bpm; }
    void snapToTargets() { if (prepared_) cutoffNow_ = target_[Cutoff]; }
    void process(float** ch, int numCh, int n) { processWithSidechain(ch, numCh, n, nullptr, 0); }
    void processWithSidechain(float** ch, int numCh, int n, const float* const* sc, int scCh);
    int latencySamples() const { return 0; }
    double modulator() const { return m_; }          // 0 .. 1 (the display)
    double cutoffInUse() const { return cutoffNow_; }

private:
    struct Chan { double s1 = 0, s2 = 0; OsSwitch os; };
    void updateMod(double level);
    double fs_ = 48000.0, bpm_ = 0.0, env_ = 0.0, envDb_ = -120.0, hi_ = -60.0, lo_ = -90.0, m_ = 0.0, ph_ = 0.0, cutoffNow_ = 1200.0;
    static constexpr int kBlocks = 20;
    double blkMax_ = -200.0, blkMin_ = 1e9;
    int blkCount_ = 0, ringPos_ = 0;
    std::array<double, kBlocks> ringMax_{}, ringMin_{};
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<Chan, 2> ch_;
};

}  // namespace sw::cr01
