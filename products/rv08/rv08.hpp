// SW RV08 Gated — a dense reverb cut by a gate that opens on the attack (spec: 仕様書 v1.0「RV08 Gated」). Wet signal only (Mix is the shared frame's).
//   mid -> 4 diffusion all-passes -> 16-line FDN (sw::Fdn) -> gate gain -> tone tilt. Size scales the line lengths (x0.5 .. x2) and sets the FDN decay (0.6 .. 2.6 s,
//   long on purpose: the gate, not the decay, shapes the tail). The detector (5 ms attack / 15 ms release peak follower on the mono input) opens the gate when it rises
//   above Threshold (-60 .. 0 dBFS) after having been below threshold - 6 dB (a new hit restarts the time). While open for Gate time T (50 .. 800 ms) the gain is
//   g(x) = (1 - Shape) + Shape * x, x = t / T (Flat = 1 all through; Reverse = a ramp up from 0 to 1), then 3 ms to close. Tone is a first-order tilt around 1 kHz, +-6 dB.
//   Snare key (rv08.evo.on): the detector listens to 150..250 Hz and 2..5 kHz only (a 4th-order band-pass at 200 Hz and a 4th-order 2..5 kHz band-pass), so bleed outside the snare's bands does not open the gate.
#pragma once
#include "sw/bleed_learner.hpp"
#include "sw/fdn.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::rv08 {

enum ParamId { Size, GateTime, Threshold, Shape, Tone, Mix, Snare, Unit, kNumParams };

const std::vector<ParamSpec>& specs();
double thresholdDbfs(double value);                  // 0..10 -> -60..0 dBFS
double gateGain(double x, double shape);             // gain at x = t / T in [0, 1]

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    // Learn (EVO, class B; the same learner as DY04's / CS02's): the key as the gate hears it (the snare's bands) is listened to for at most kLearnSeconds (or until learn() is called again); then
    // Threshold goes between the snare and the bleed and Snare key is switched on. The core writes the two to the host (takeParamWrite: bit 0 begin, 1 value, 2 end). Audio thread.
    static constexpr double kLearnSeconds = 30.0;
    void learn();
    bool learning() const { return learner_.learning(); }
    double learnProgress() const { return learner_.progress(); }
    int learnOnsets() const { return learner_.onsets(); }
    bool learnedOk() const { return learnedOk_; }
    int takeParamWrite(int& id, double& plain);

private:
    struct Ap { std::vector<float> buf; size_t pos = 0; double process(double x, double g) { const double d = buf[pos]; const double y = -g * x + d; buf[pos] = static_cast<float>(x + g * y); if (++pos >= buf.size()) pos = 0; return y; } };
    void updateLines();
    double fs_ = 48000.0, learnEnv_ = 0.0, env_ = 0.0, gate_ = 0.0, t_ = 0.0, lateTrim_ = 1.0, tiltLp_[2] = {0, 0};
    bool open_ = false, armed_ = true, prepared_ = false;
    std::array<double, kNumParams> target_{};
    Fdn fdn_;
    std::array<Ap, 4> ap_{};
    std::array<Svf, 2> keyLow_{};
    std::array<Svf, 4> keyHigh_{};
    void applyLearned(const BleedLearner::Result& r);
    BleedLearner learner_;
    std::array<std::pair<int, double>, 2> writes_{};
    int nWrites_ = 0, writeAt_ = 0;
    bool wasLearning_ = false, learnedOk_ = false;
};

}  // namespace sw::rv08
