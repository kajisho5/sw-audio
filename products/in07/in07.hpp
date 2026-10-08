// SWINGBY (SW IN07) — lightweight preset synth (added 2026-10-08 at the client's request; IN01..IN06 stay unbuilt).
//   No section in spec v1.0: every value here is a design value (README「IN07 の設計」). The plan: claude/IN07_synth_plan.md in the project docs.
//   Four layers (L1..L4), each a full voice: oscillator (1..8 unison copies) -> drive -> filter -> amp, with an amp envelope and a filter
//   envelope, its own level, pan and pitch offsets. A note plays every layer that is on. Polyphony 1..32 notes; Mono and Legato modes.
//   Light by construction: a silent voice returns at once (sleep), the filter coefficients and the layer gains move at control rate (every 32
//   samples, linear ramps in between), the 2x oversampler runs only around the drive stage and only when Drive > 0 at note-on, a layer that
//   is off costs nothing.
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
//   Velocity: amplitude (1 - s) + s v^2 (s = Vel sens). Glide: constant time, linear in semitones.
//   Layer: level -60 .. +6 dB (-60 = Off), pan equal power with the centre at unity (as the unison spread), pitch = key + 12 Octave + Semi + Fine / 100 + bend.
//   Notes: Poly = a note per key (the same key again re-triggers its own voice); at the voice limit the oldest released note goes, else the oldest
//   held one, with a 3 ms fade (spare slots let it fade while the new note starts). Glide in Poly starts from the previous note while one is held.
//   Mono = one voice, last-note priority, every new pitch re-triggers, glides whenever Glide > 0. Legato = one voice, no new attack while a key is
//   held, glides only between held keys. Sustain pedal holds released notes. Every note reports its end once (CLAP note end).
//   Parameter changes reach the voices at control rate. Unison count, the drive path and the start phases are taken at note-on.
#pragma once
#include "sw/oversample.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace sw::in07 {

// host ids: the global parameters, then each layer's block (lp(layer, param))
enum GlobalId { Voices, Mode, Glide, Bend, Level, kNumGlobal };
enum LayerParam { On, LayerLevel, Pan, Wave, PulseWidth, Octave, Semi, Fine, Unison, Detune, Spread, FilterType, Cutoff, Resonance, Drive, FilterEnv,
                  KeyTrack, AmpA, AmpD, AmpS, AmpR, FenvA, FenvD, FenvS, FenvR, VelSens, kLayerParams };
constexpr int kLayers = 4;
constexpr int kNumParams = kNumGlobal + kLayers * kLayerParams;
constexpr int lp(int layer, int param) { return kNumGlobal + layer * kLayerParams + param; }
enum ModeId { Poly = 0, Mono = 1, Legato = 2 };
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
    void quickRelease(double seconds);   // a fast release (voice stealing): to -80 dB in `seconds`, whatever the Release time
    double next();
    void reset() { stage_ = Idle; level_ = 0.0; quick_ = false; }
    bool active() const { return stage_ != Idle; }
    Stage stage() const { return stage_; }
    double level() const { return level_; }

private:
    void update();
    double fs_ = 48000.0, a_ = 0.005, d_ = 0.3, s_ = 0.7, r_ = 0.4;
    double ca_ = 0.0, cd_ = 0.0, cr_ = 0.0, cq_ = 0.0, sus_ = 0.7, level_ = 0.0;
    int dN_ = 1, rN_ = 1, qN_ = 1, count_ = 0;
    bool dirty_ = true, quick_ = false;
    Stage stage_ = Idle;
};

// what every voice reads from the synth
struct Shared {
    double bendSemis = 0.0;   // pitch bend now (semitones)
    double glideMs = 0.0;
};

// one layer of one note
class Voice {
public:
    static constexpr int kMaxUnison = 8;
    static constexpr int kCtl = 32;   // control-rate period (samples)

    void prepare(double fs, const double* layerParams, const Shared* shared);
    // glideFrom: the key to glide from (a negative value = no glide); the time is Shared::glideMs
    void noteOn(int key, double velocity, double glideFrom);
    void legato(int key, double glideFrom);   // a new key, no new attack
    void noteOff();
    void kill();                               // a 3 ms fade (stealing, a layer turned off)
    void reset();                              // silent at once
    void render(float* l, float* r, int n);    // adds into l and r
    bool active() const { return amp_.active(); }
    bool killed() const { return killed_; }
    double cutoffInUse() const { return cutoff_; }
    double frequency() const;                  // the pitch in use (Hz), with the layer offsets and the bend
    double keyInUse() const { return key_; }   // the (gliding) key, without the layer offsets
    int key() const { return note_; }
    const Adsr& ampEnv() const { return amp_; }

private:
    void control();
    double p(int id) const { return lp_[id]; }
    const double* lp_ = nullptr;
    const Shared* sh_ = nullptr;
    double fs_ = 48000.0, key_ = 60.0, glideFrom_ = 60.0, velGain_ = 1.0, cutoff_ = 2400.0, drive_ = 0.0, pitch_ = 60.0;
    double gL_ = 1.0, gR_ = 1.0, gL0_ = 1.0, gR0_ = 1.0;   // the layer's level and pan, ramped over each control period
    int note_ = 60, unison_ = 1, ctl_ = 0, glideN_ = 0, glideLeft_ = 0, type_ = LP24;
    bool oversample_ = false, first_ = true, stereo_ = false, killed_ = false;
    uint32_t rng_ = 0x5EED1234u;
    std::array<BlepOsc, kMaxUnison> osc_;
    std::array<double, kMaxUnison> detune_{}, gl_{}, gr_{};
    std::array<std::array<Svf, 2>, 2> svf_;            // [channel][stage]
    std::array<Oversampler2x, 2> os_;
    Adsr amp_, fenv_;
};

class Processor {
public:
    static constexpr int kMaxVoices = 32;               // the top of the Voices parameter
    static constexpr int kSlots = kMaxVoices + 8;       // spare slots: a stolen note fades out while the new one starts

    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    double param(int id) const { return (id >= 0 && id < kNumParams) ? target_[static_cast<size_t>(id)] : 0.0; }
    void snapToTargets() {}
    // velocity 0..1; channel and noteId are only carried to the end report (CLAP); channel -1 / key -1 in noteOff = any
    void noteOn(int key, double velocity, int channel = 0, int noteId = -1);
    void noteOff(int key, int channel = -1);
    void choke(int key, int channel = -1);              // stop at once (3 ms)
    void pitchBend(double v);                           // -1 .. +1 of the bend range
    void sustain(bool down);
    void allNotesOff();                                 // release everything (the pedal is lifted too)
    void allSoundOff();                                 // silent at once
    void process(float** ch, int numCh, int n);         // writes (replaces) the output
    int latencySamples() const { return 0; }
    bool active() const;                                // anything sounding
    int notes() const;                                  // notes sounding, without the ones fading after a steal
    const Voice* find(int key, int layer = 0) const;    // the sounding (not fading) voice of a key on a layer, or null
    bool takeEnded(int& key, int& channel, int& noteId); // notes whose sound has ended, oldest first

private:
    struct Slot {
        std::array<Voice, kLayers> v;
        int key = -1, channel = 0, noteId = -1;
        bool held = false, sustained = false, stolen = false;
        uint64_t age = 0;
        bool sounding() const;
    };
    void start(Slot& s, int key, double vel, int channel, int noteId, double glideFrom);
    void release(Slot& s);
    void end(Slot& s);                                  // report the note's end
    Slot* allocate();
    void pushEnded(int key, int channel, int noteId);
    void monoOn(int key, double vel, int channel, int noteId);
    void monoOff(int key);
    double p(int id) const { return target_[static_cast<size_t>(id)]; }

    double fs_ = 48000.0, lastVel_ = 1.0, bend_ = 0.0, lastKey_ = -1.0;
    bool prepared_ = false, pedal_ = false;
    uint64_t clock_ = 0;
    int mode_ = Poly;
    std::array<double, kNumParams> target_{};
    std::array<bool, kLayers> layerOn_{};
    Shared shared_;
    std::array<Slot, kSlots> slots_;
    std::vector<int> held_;                             // mono / legato: the keys held, in order
    int monoSlot_ = -1;
    struct Ended { int key, channel, noteId; };
    static constexpr size_t kEnded = 256;
    std::array<Ended, kEnded> ended_{};
    size_t endHead_ = 0, endTail_ = 0;
    std::vector<float> l_, r_;
    LinearSmoother level_;
};

}  // namespace sw::in07
