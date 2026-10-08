// SW IN07 [name TBD] — lightweight preset synth, engine prototype (added 2026-10-08 at the client's request; IN01..IN06 stay unbuilt).
//   No section in spec v1.0: every value here is a design value (README「IN07 の設計」). The plan: claude/IN07_synth_plan.md in the project docs.
//   One voice = oscillator (1..8 unison copies) -> drive -> filter -> amp, with an amp envelope and a filter envelope. The voice is written so the
//   coming voice allocator can run N of them; Processor here is a monophonic shell (last-note priority) for tests and listening.
//   Light by construction: a silent voice returns at once (sleep), the filter coefficients move at control rate (every 32 samples, linear coefficient
//   ramps in between), the 2x oversampler runs only around the drive stage and only when Drive > 0 at note-on.
//   Oscillator: naive waveform + minBLEP at each jump (saw, square: a 64-sample minimum-phase band-limited step, Blackman-windowed sinc at 0.45 fs made
//   minimum phase through the real cepstrum, 64 sub-sample phases with linear interpolation; no delay) and 2-point polyBLAMP at each corner (triangle);
//   sine is exact. The cost of minBLEP is per jump (64 adds), not per sample. A minimum-phase step is late by its centroid (about 3.3 samples) while the
//   naive ramp is not, which leaves a DC offset growing with pitch; so the naive saw / square is read that much late (the correction for a jump J is
//   J (step(x) - [x >= delay])), and the result equals the naive waveform through the minimum-phase low-pass: no DC, 0.07 ms late.
//   The pulse has its mean (2 PW - 1) taken out, so no width leaves DC either.
//   Unison: copies spread +-50 cents x Detune (evenly), panned evenly across Spread (equal power, centre = unity), summed / sqrt(N);
//   random start phases from a fixed-seed generator when N > 1 (repeatable renders), phase 0 when N = 1.
//   Filter: TPT SVF (sw::Svf). LP24 = Q 0.5412 then Q 1.3066 x (25 / 1.3066)^Res (Res 0 = 4th-order Butterworth); LP12 / BP12 / HP12 = one section,
//   Q 0.7071 x (25 / 0.7071)^Res. Cutoff x 2^(5 x Env x fenv) x 2^(Key x (note - 60) / 12), clamped to 20 Hz .. 0.45 fs.
//   Drive 0 .. 100 %: G = 1 + 9 d, x + d (tanh(G x) / G - x) at 2x (unity gain for small signals, Drive 0 = exactly linear and not oversampled;
//   tanh is a [7/6] Pade approximant within 1e-4).
//   Envelopes: attack = exponential approach to 1.2, reaching 1.0 at the set time (from 0); decay = to within 0.1 % (-60 dB) of the step at the set time,
//   then Sustain; release = to -80 dB of its start level at the set time, then 0 and the voice sleeps. Re-trigger starts the attack from the current level.
//   Velocity: amplitude (1 - s) + s v^2 (s = Vel sens). Glide: constant time, linear in semitones, from the pitch in use.
//   Parameter changes reach the voice at control rate. Unison count, the drive path and the start phases are taken at note-on.
#pragma once
#include "sw/oversample.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace sw::in07 {

enum ParamId { Wave, PulseWidth, Octave, Unison, Detune, Spread, FilterType, Cutoff, Resonance, Drive, FilterEnv, KeyTrack,
               AmpA, AmpD, AmpS, AmpR, FenvA, FenvD, FenvS, FenvR, VelSens, Glide, Level, kNumParams };
enum WaveId { Sine = 0, Triangle = 1, Saw = 2, Square = 3 };
enum FilterTypeId { LP12 = 0, LP24 = 1, BP12 = 2, HP12 = 3 };

const std::vector<ParamSpec>& specs();

// the band-limited unit step for minBLEP: 64 output samples long, minimum phase (no delay), 64 sub-sample phases
struct MinBlep {
    static constexpr int kTaps = 64, kRes = 64;
    std::array<double, kTaps * kRes + 1> step{};   // rises from about 0 to 1
    double delay = 0.0;                              // its centroid (samples): the naive waveform runs this much late so the ramps and the steps line up
    double operator()(double x) const;               // the step x samples after the discontinuity (1 from kTaps on)
};
const MinBlep& minBlep();

// one band-limited oscillator; the phase runs 0..1
class BlepOsc {
public:
    void setWave(int w) { wave_ = w; }
    void setPulseWidth(double pw) { pw_ = pw; }
    void setIncrement(double inc) { inc_ = inc; }   // f / fs, below 0.5
    void setPhase(double p) { ph_ = p; ring_.fill(0.0f); }
    void setCorrection(bool on) { correct_ = on; }  // false = the naive waveform (tests compare the two)
    double phase() const { return ph_; }
    double next();

private:
    void jump(double after, double height);         // a jump of `height` that happened `after` samples before the next sample
    double late(double t) const;
    int wave_ = Saw, pos_ = 0;
    double pw_ = 0.5, inc_ = 0.0, ph_ = 0.0;
    bool correct_ = true;
    std::array<float, MinBlep::kTaps> ring_{};       // pending corrections, one per coming sample
};

class Adsr {
public:
    enum Stage { Idle, Attack, Decay, Sustain, Release };
    void prepare(double fs) { fs_ = fs; dirty_ = true; }
    void setTimes(double attackS, double decayS, double sustain, double releaseS);
    void gate(bool on);
    double next();
    void reset() { stage_ = Idle; level_ = 0.0; }
    bool active() const { return stage_ != Idle; }
    Stage stage() const { return stage_; }
    double level() const { return level_; }

private:
    void update();
    double fs_ = 48000.0, a_ = 0.005, d_ = 0.3, s_ = 0.7, r_ = 0.4;
    double ca_ = 0.0, cd_ = 0.0, cr_ = 0.0, sus_ = 0.7, level_ = 0.0;
    int dN_ = 1, rN_ = 1, count_ = 0;
    bool dirty_ = true;
    Stage stage_ = Idle;
};

class Voice {
public:
    static constexpr int kMaxUnison = 8;
    static constexpr int kCtl = 32;   // control-rate period (samples)

    void prepare(double fs, const std::array<double, kNumParams>* params);
    void noteOn(int note, double velocity, bool glide);
    void noteOff();
    void render(float* l, float* r, int n);   // adds into l and r
    bool active() const { return amp_.active(); }
    double cutoffInUse() const { return cutoff_; }
    double frequency() const;                  // the pitch in use (Hz)
    int note() const { return note_; }
    const Adsr& ampEnv() const { return amp_; }

private:
    void control();
    double p(int id) const { return (*params_)[static_cast<size_t>(id)]; }
    const std::array<double, kNumParams>* params_ = nullptr;
    double fs_ = 48000.0, pitch_ = 60.0, glideFrom_ = 60.0, velGain_ = 1.0, cutoff_ = 2400.0, drive_ = 0.0;
    int note_ = 60, unison_ = 1, ctl_ = 0, glideN_ = 0, glideLeft_ = 0, type_ = LP24;
    bool oversample_ = false, first_ = true, stereo_ = false;
    uint32_t rng_ = 0x5EED1234u;
    std::array<BlepOsc, kMaxUnison> osc_;
    std::array<double, kMaxUnison> detune_{}, gl_{}, gr_{};
    std::array<std::array<Svf, 2>, 2> svf_;            // [channel][stage]
    std::array<Oversampler2x, 2> os_;
    Adsr amp_, fenv_;
};

// monophonic shell for tests and listening (last-note priority); polyphony comes with the voice allocator
class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void noteOn(int note, double velocity);    // velocity 0..1
    void noteOff(int note);
    void allNotesOff();
    void process(float** ch, int numCh, int n);   // writes (replaces) the output
    int latencySamples() const { return 0; }
    bool active() const { return voice_.active(); }
    const Voice& voice() const { return voice_; }

private:
    double fs_ = 48000.0, lastVel_ = 1.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::vector<int> held_;
    std::vector<float> l_, r_;
    LinearSmoother level_;
    Voice voice_;
};

}  // namespace sw::in07
