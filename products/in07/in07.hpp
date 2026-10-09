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
//   then Sustain (Sustain 0: the voice sleeps there, even with the key held); release = to -80 dB of its start level at the set time, then 0 and the voice sleeps. Re-trigger starts the attack from the current level.
//   Velocity: amplitude (1 - s) + s v^2 (s = Vel sens). Glide: constant time, linear in semitones.
//   Layer: level -60 .. +6 dB (-60 = Off), pan equal power with the centre at unity (as the unison spread), pitch = key + 12 Octave + Semi + Fine / 100 + bend.
//   Notes: Poly = a note per key (the same key again re-triggers its own voice); at the voice limit the oldest released note goes, else the oldest
//   held one, with a 3 ms fade (spare slots let it fade while the new note starts). Glide in Poly starts from the previous note while one is held.
//   Mono = one voice, last-note priority, every new pitch re-triggers, glides whenever Glide > 0. Legato = one voice, no new attack while a key is
//   held, glides only between held keys. Sustain pedal holds released notes. Every note reports its end once (CLAP note end).
//   Oscillator types (osc.hpp): Analog (the shapes above), Wavetable (8 tables x 16 frames, mipmapped; Position sweeps the frames, ramped at control
//   rate), FM (2 operators: ratio, index 0..10 rad with a decay to -60 dB in the set time or Hold, modulator feedback; the index is held under the
//   aliasing limit), Sample (5 loops, 3 one-shots; the key sets the speed from C4). Unison, detune, spread and gravity work on every type (gravity
//   pulls the carrier phases; samples have no phase to pull, so it does nothing there). The type and the sample are taken at a fresh note-on.
//   Parameter changes reach the voices at control rate. Unison count, the drive path and the start phases are taken at note-on.
//   After the voices: the effect chain (fx.hpp), then Level.
#pragma once
#include "sw/oversample.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include "in07/fx.hpp"
#include "in07/osc.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace sw::in07 {

// host ids: the global parameters, then each layer's block (lp(layer, param))
enum GlobalId { Voices, Mode, Glide, Bend, Level, kNumGlobal };
enum LayerParam { On, LayerLevel, Pan, OscType, Wave, PulseWidth, Table, Position, FmRatio, FmIndex, FmDecay, FmFeedback, SampleId,
                  Octave, Semi, Fine, Unison, Detune, Spread, Gravity, FilterType, Cutoff, Resonance, Drive, FilterEnv,
                  KeyTrack, AmpA, AmpD, AmpS, AmpR, FenvA, FenvD, FenvS, FenvR, VelSens, kLayerParams };
constexpr int kLayers = 4;
constexpr int lp(int layer, int param) { return kNumGlobal + layer * kLayerParams + param; }
// the effects (fx.hpp): the order slots (not automatable), then four parameters per effect (On first)
constexpr int kFxBase = kNumGlobal + kLayers * kLayerParams;
enum FxParamId { FxSlot1 = kFxBase, FxSlot2, FxSlot3, FxSlot4, FxSlot5, FxSlot6,
                 FxDriveOn, FxDriveAmount, FxDriveTone, FxDriveMix, FxChorusOn, FxChorusRate, FxChorusDepth, FxChorusMix,
                 FxDelayOn, FxDelayTime, FxDelayFeedback, FxDelayMix, FxReverbOn, FxReverbSize, FxReverbDamp, FxReverbMix,
                 FxEqOn, FxEqLow, FxEqMid, FxEqHigh, FxLimitOn, FxLimitGain, FxLimitCeiling, FxLimitRelease, kFxEnd };
constexpr int kFxParams = kFxEnd - kFxBase;
constexpr int fxOnId(int fx) { return FxDriveOn + 4 * fx; }
// modulation: two LFOs, eight macros (each with its own job, the middle = no change), the flyby, then the matrix (8 slots x On / Source / Dest / Amount)
enum ModParamId { Lfo1Shape = kFxEnd, Lfo1Rate, Lfo1Sync, Lfo1Ecc, Lfo1Trigger, Lfo2Shape, Lfo2Rate, Lfo2Sync, Lfo2Ecc, Lfo2Trigger,
                  Macro1, Macro2, Macro3, Macro4, Macro5, Macro6, Macro7, Macro8,
                  FlybyMode, FlybyDepth, FlybyTime, FlybyNear, FlybySide, kModSlotBase };
constexpr int kModSlots = 8;
enum ModField { ModOn, ModSrc, ModDst, ModAmount, kModFields };
constexpr int modId(int slot, int field) { return kModSlotBase + slot * kModFields + field; }
// the preset selector (Init, then the factory presets; not automatable): the plug-in layer loads the preset when the host changes it,
// the engine only keeps the value (a restored session keeps its own values)
constexpr int PresetSelect = kModSlotBase + kModSlots * kModFields;
// the arpeggiator and the trance gate (appended after the selector, 2026-10-09): 16 steps each
constexpr int kArpSteps = 16;
enum ArpParamId { ArpOn = PresetSelect + 1, ArpMode, ArpRate, ArpOctaves, ArpLength, ArpSwing, ArpSteps, kArpVelBase };
constexpr int arpVel(int i) { return kArpVelBase + i; }                  // 0..100 % (0 = a rest)
constexpr int arpPitch(int i) { return kArpVelBase + kArpSteps + i; }    // -12 / 0 / +7 / +12 semitones
enum GateParamId { GateOn = kArpVelBase + 2 * kArpSteps, GateRate, GateDepth, kGateStepBase };
constexpr int gateStep(int i) { return kGateStepBase + i; }               // Off / On
constexpr int kNumParams = kGateStepBase + kArpSteps;
enum ArpModeId { ArpUp = 0, ArpDown, ArpUpDown, ArpOrder, ArpRandom };
enum LfoShapeId { LfoOrbit = 0, LfoTriangle, LfoSaw, LfoSquare, LfoRandom };
enum ModSource { SrcNone = 0, SrcLfo1, SrcLfo2, SrcEnv2, SrcVelocity, SrcModWheel, SrcAftertouch, SrcKey,
                 SrcM1, SrcM2, SrcM3, SrcM4, SrcM5, SrcM6, SrcM7, SrcM8, kModSources };
enum ModDest { DstNone = 0, DstCutoff, DstResonance, DstPitch, DstDrive, DstPan, DstLevel, DstL1Level, DstL2Level, DstL3Level, DstL4Level,
               DstLfo1Rate, DstLfo2Rate, DstPulseWidth, DstDetune, DstGravity, DstWtPos, DstFmIndex, kModDests };
enum FlybyModeId { FlybyOff = 0, FlybyArrive, FlybyPass, FlybyLeave };

// an LFO's value (-1..1) at a phase (0..1). Orbit: the moon's place along the long axis of a Kepler ellipse, cos E with E - e sin E = 2 pi phase
// (e = eccentricity 0..0.95; e = 0 is a cosine; near 1 it lingers far out at -1 and whips round the planet to +1). Random: a value per cycle from `seed`.
double lfoShape(int shape, double phase, double ecc, uint32_t seed = 0);
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

// the voice drive's 2x oversampler: 8 coefficients, transition band 0.06 (stop band about -113 dB from 26.9 kHz at 48 kHz, pass band to
// 21.1 kHz). The drive's own folding at the 2x rate sets the alias floor long before that (README「IN07 の評価」), and it costs two thirds
// of the standard 12-coefficient one, per voice and layer.
using VoiceOs = StereoOversampler2xN<8>;   // both channels of a voice at once
constexpr double kVoiceOsTbw = 0.06;

// one band-limited oscillator; the phase runs 0..1
class BlepOsc {
public:
    void setWave(int w) { wave_ = w; }
    void setPulseWidth(double pw) { pw_ = pw; }
    void setIncrement(double inc) { inc_ = inc; late_ = minBlep().delay * inc; }   // f / fs, below 0.5
    void setPhase(double p) { ph_ = p; ring_.fill(0.0f); }
    void setCorrection(bool on) { correct_ = on; }  // false = the naive waveform (tests compare the two)
    double phase() const { return ph_; }
    double next();
    template <int W> double step();                 // next() when the wave is known to be W (the caller picks it once per block)
    int wave() const { return wave_; }

private:
    void jump(double after, double height);         // a jump of `height` that happened `after` samples before the next sample
    double late(double t) const;
    int wave_ = Saw, pos_ = 0;
    double pw_ = 0.5, inc_ = 0.0, ph_ = 0.0, late_ = 0.0;   // late_: minBLEP's delay in phase (delay x increment)
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

// what every voice reads from the synth (the global sources are updated every 32 samples on the synth's own grid)
struct Shared {
    double bendSemis = 0.0;   // pitch bend now (semitones)
    double glideMs = 0.0;
    double src[kModSources] = {};   // the global sources now (LFOs, wheel, aftertouch, macros -1..1); the per-voice ones are filled by each voice
    struct Slot { int src = SrcNone, dst = DstNone; double amount = 0.0; };
    std::array<Slot, kModSlots> slots{};
    int nSlots = 0;           // the slots in use (on, with a source and a destination), packed first
    bool driveRouted = false; // something can add drive (decides the voice's 2x path at note-on)
    int gridLeft = 0;         // samples to the synth's next control point: a new note's second control period starts there
    // flyby
    int flyMode = FlybyOff;
    double flyDepth = 0.6, flyTime = 1.5, flyDelta = 0.35, flySign = 1.0;
};

// one layer of one note
class Voice {
public:
    static constexpr int kMaxUnison = 8;
    static constexpr int kCtl = 32;   // control-rate period (samples)

    void prepare(double fs, const double* layerParams, const Shared* shared, int layer = 0);
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
    double coherence() const { return coherence_; }   // how much the unison copies move together (0..1; the gravity's mean field)
    int key() const { return note_; }
    const Adsr& ampEnv() const { return amp_; }

private:
    void control();
    void oscillate(double* xl, double* xr, int m, int done);
    template <int W> void analog(double* xl, double* xr, int m);   // the Analog copies with the wave W   // the unison copies' sum for m samples (done: samples of the period already played)
    double phaseOf(int k) const { return otype_ == OscAnalog ? osc_[static_cast<size_t>(k)].phase() : ph_[static_cast<size_t>(k)]; }
    double p(int id) const { return lp_[id]; }
    const double* lp_ = nullptr;
    const Shared* sh_ = nullptr;
    double fs_ = 48000.0, key_ = 60.0, glideFrom_ = 60.0, velGain_ = 1.0, cutoff_ = 2400.0, drive_ = 0.0, pitch_ = 60.0;
    double gL_ = 1.0, gR_ = 1.0, gL0_ = 1.0, gR0_ = 1.0;   // the layer's level and pan, ramped over each control period
    double vel_ = 1.0, coherence_ = 0.0, flyT_ = 0.0, flySign_ = 1.0, levDb_ = 1e9, levGain_ = 1.0;
    int layer_ = 0;
    bool flyOn_ = false;
    std::array<double, kMaxUnison> inc0_{};
    int note_ = 60, unison_ = 1, ctl_ = 0, glideN_ = 0, glideLeft_ = 0, type_ = LP24, align_ = 0;
    bool oversample_ = false, first_ = true, stereo_ = false, killed_ = false;
    uint32_t rng_ = 0x5EED1234u;
    std::array<BlepOsc, kMaxUnison> osc_;
    std::array<double, kMaxUnison> detune_{}, gl_{}, gr_{};
    // the other oscillator types: per copy the (carrier) phase, the modulator phase and its last two outputs, the sample position (level-0 samples)
    int otype_ = OscAnalog, table_ = 0, sample_ = 0;
    std::array<double, kMaxUnison> ph_{}, pm_{}, fb0_{}, fb1_{}, spos_{}, sinc_{}, sscale_{}, sblend_{};
    std::array<const float*, kMaxUnison> wbase_{}, sptr_{}, sptr2_{};   // sptr2_: the next level, faded in by sblend_ near a level's limit
    std::array<int, kMaxUnison> wsize_{};
    std::array<bool, kMaxUnison> sdone_{};
    double wp0_ = 0.0, wp1_ = 0.0;                        // the wavetable position (frames) at the start and the end of the control period
    double fi0_ = 0.0, fi1_ = 0.0, fb0k_ = 0.0, fb1k_ = 0.0, fmEnv_ = 1.0, fmRatio_ = 1.0;   // FM index and feedback (cycles), the index decay
    std::array<double, 2> dcx_{}, dcy_{};                 // the FM output's DC blocker
    double dca_ = 0.9993;
    std::array<StereoSvf, 2> svf_;                     // [stage]: both channels in one SIMD pair (the same coefficients)
    VoiceOs os_{kVoiceOsTbw};                          // the drive's 2x, both channels
    Adsr amp_, fenv_;
};

class Processor {
public:
    static constexpr int kMaxVoices = 32;               // the top of the Voices parameter
    static constexpr int kSlots = kMaxVoices + 8;       // spare slots: a stolen note fades out while the new one starts

    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    double param(int id) const {   // the value last set (one waiting for a patch change included)
        if (id < 0 || id >= kNumParams) return 0.0;
        return stagedSet_[static_cast<size_t>(id)] ? staged_[static_cast<size_t>(id)] : target_[static_cast<size_t>(id)];
    }
    void snapToTargets() { fx_.snapSwitches(); }
    void setTempo(double bpm) { if (bpm > 0.0) { bpm_ = std::clamp(bpm, 30.0, 300.0); fx_.setTempo(bpm); } }   // the host tempo (delay, LFO sync); 120 until told
    std::array<int, kFx> fxOrder() const { return fx_.order(); }
    double limiterPeak() const { return fx_.limiterPeak(); }   // the peak into the Limit effect since the last reset (linear)
    void resetLimiterPeak() { fx_.resetLimiterPeak(); }
    // velocity 0..1; channel and noteId are only carried to the end report (CLAP); channel -1 / key -1 in noteOff = any.
    // Channels 0..15 are the host's (MIDI), 16 the plug-in window's; the arpeggiator plays its notes on kArpChannel, and their ends are
    // never reported (the host did not send them: the key it holds ends when it lets go of it).
    static constexpr int kArpChannel = 17;
    void noteOn(int key, double velocity, int channel = 0, int noteId = -1);
    void noteOff(int key, int channel = -1);
    void choke(int key, int channel = -1);              // stop at once (3 ms)
    void pitchBend(double v);                           // -1 .. +1 of the bend range
    void modWheel(double v);                            // 0..1 (CC 1)
    void aftertouch(double v);                          // 0..1 (channel pressure)
    void sustain(bool down);
    void allNotesOff();                                 // release everything (the pedal is lifted too)
    void allSoundOff();                                 // silent at once
    void process(float** ch, int numCh, int n);         // writes (replaces) the output
    // the host's transport at the start of the next process() call (the arp and the gate follow its beat while it plays; stopped, they
    // run on their own clock, which starts at 0 with a note played while no key is held)
    void setTransport(bool playing, double beats);
    bool keyHeld(int key) const;                        // a note of this key is sounding and held (by a key, the pedal or the arp)
    // a whole patch (a preset): beginPatch(), setParam() for each of its values, endPatch(). Nothing sounding (or not prepared): the values
    // go in at once. Otherwise the change waits: the output fades out over kPatchFadeMs, then the values go in, every voice and effect
    // starts again from silence, and the keys still held (and the notes the pedal holds) play again with the new patch (no step, and the
    // held chord sounds as the new patch, not a mix of both). Values set during the fade wait for it too; param() reports them at once.
    static constexpr double kPatchFadeMs = 8.0;
    void beginPatch() { staging_ = true; }
    void endPatch();
    bool patchPending() const { return staging_ || fading_; }
    int latencySamples() const { return 0; }
    bool active() const;                                // any voice sounding (the effects' tails are not counted)
    bool fxAsleep() const;                              // the effects are idle (no voice, tails under -120 dBFS for 0.5 s)
    int notes() const;                                  // notes sounding, without the ones fading after a steal
    const Voice* find(int key, int layer = 0) const;    // the sounding (not fading) voice of a key on a layer, or null
    bool takeEnded(int& key, int& channel, int& noteId); // notes whose sound has ended, oldest first

private:
    struct Slot {
        std::array<Voice, kLayers> v;
        int key = -1, channel = 0, noteId = -1;
        double vel = 1.0;
        bool held = false, sustained = false, stolen = false, arp = false;   // arp: a note the arpeggiator plays
        uint64_t age = 0;
        bool sounding() const;
    };
    void start(Slot& s, int key, double vel, int channel, int noteId, double glideFrom);
    void release(Slot& s);
    void end(Slot& s);                                  // report the note's end
    Slot* allocate();
    void pushEnded(int key, int channel, int noteId);
    void monoOn(int key, double vel, int channel, int noteId);
    void monoOff(int key, int channel);
    // mono / legato keep the keys held as (channel, key): the window's key and the host's same key are two keys
    static int heldCode(int key, int channel) { return std::clamp(channel, 0, 31) * 128 + std::clamp(key, 0, 127); }
    static int heldKey(int code) { return code % 128; }
    static int heldChannel(int code) { return code / 128; }
    void playOn(int key, double vel, int channel, int noteId);   // a note to the voices (poly, mono, legato)
    void playOff(int key, int channel);
    // the arpeggiator: the keys it plays, its clock and its steps
    struct ArpKey { int key = -1, channel = 0, noteId = -1; double vel = 1.0; uint64_t order = 0; bool held = true; };
    void arpAdd(int key, double vel, int channel, int noteId);
    void arpRelease(int key, int channel);
    void arpRemove(int i, bool report);
    void arpSwitch(bool on);                            // the Arp On parameter changed: the held keys move to the arp or back to the voices
    void arpEvents();                                   // the steps and note ends due now
    int arpSamplesToNext() const;                       // to the next step or note end (1 at least)
    void arpStep(long k);
    double stepBeats() const;                           // one step of the arp
    double stepStart(long k) const;                     // where step k starts (beats; swing on the odd steps)
    void resetClock();                                  // its own clock from 0 (the transport is not playing)
    void resyncSteps();                                 // the next step from the beat where the clock is now
    bool anyKeyHeld() const;
    double beatsPerSample() const { return bpm_ / 60.0 / fs_; }
    double p(int id) const { return target_[static_cast<size_t>(id)]; }
    void setNow(int id, double v);                      // a (normalised) value goes in
    void applyStaged();                                 // the values a patch change held back go in
    void swapPatch();                                   // the end of the fade: the new patch, the held notes again
    void updateFx();                                    // the effects' parameters and order from the table
    void updateMod();                                   // the matrix, the macros and the flyby settings into Shared
    void tick();                                        // the global sources, every 32 samples

    double fs_ = 48000.0, lastVel_ = 1.0, bend_ = 0.0, lastKey_ = -1.0;
    bool prepared_ = false, pedal_ = false;
    uint64_t clock_ = 0;
    int mode_ = Poly;
    std::array<double, kNumParams> target_{};
    std::array<bool, kLayers> layerOn_{};
    Shared shared_;
    std::vector<Slot> slots_;                           // kSlots, on the heap (40 x 4 voices are about 750 kB: too big for a stack)
    std::vector<int> held_;                             // mono / legato: the keys held (heldCode), in order
    int monoSlot_ = -1;
    struct Ended { int key, channel, noteId; };
    static constexpr size_t kEnded = 256;
    std::array<Ended, kEnded> ended_{};
    size_t endHead_ = 0, endTail_ = 0;
    std::vector<float> l_, r_;
    LinearSmoother level_;
    FxChain fx_;
    FxParams fxParams_;
    int64_t fxIdle_ = 0;
    int gctl_ = 0;                                      // samples to the next global control point
    std::array<int, kSlots> active_{};                  // the slots that sound in this block
    double bpm_ = 120.0, wheel_ = 0.0, after_ = 0.0, flyFlip_ = 1.0;
    std::array<double, 2> lfoPhase_{}, lfoValue_{};
    std::array<uint32_t, 2> lfoSeed_{{0x1234567u, 0x7654321u}};                                // samples with no voice and the effects' output under -120 dBFS
    bool fresh_ = true;                                 // nothing processed since prepare: switching an effect does not fade
    std::array<double, kNumParams> staged_{};           // a patch change: the values waiting for the fade
    std::array<bool, kNumParams> stagedSet_{};
    bool staging_ = false, fading_ = false, anyStaged_ = false;
    int fadeLen_ = 1, fadeLeft_ = 0;                    // the fade before a patch change (samples)
    std::array<ArpKey, 128> arpKeys_{};                 // the keys the arp plays (held, or kept by the pedal), in the order played
    int arpN_ = 0;
    bool arpOn_ = false, arpNoteOn_ = false, arpStarting_ = false, playing_ = false;
    uint64_t arpOrder_ = 0;
    double beat_ = 0.0, arpOffBeat_ = 0.0, gateGain_ = 1.0;
    long arpK_ = -1, arpPos_ = 0;                       // the last step started; the steps that played a note (the note order)
    int arpNoteKey_ = -1;
    uint32_t arpSeed_ = 0x2545F491u;
};

}  // namespace sw::in07
